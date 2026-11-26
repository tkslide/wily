package Wily::Client;

use strict;
use warnings;
use utf8;
use Exporter qw(import);
use Encode qw(encode_utf8 decode_utf8);
use Fcntl qw(O_WRONLY);
use File::Temp qw(mktemp);
use IO::Select;
use IO::Socket::UNIX;
use Socket qw(SOCK_STREAM);

our $VERSION = '0.01';

use constant {
    # Events (also used as bitmasks)
    WEexec      => 1,
    WEgoto      => 2,
    WEdestroy   => 4,
    WEreplace   => 8,
    WEfencepost => 9,

    # Requests and Responses
    WRerror     => 10,
    WMlist      => 11,
    WRlist      => 12,
    WMnew       => 13,
    WRnew       => 14,
    WMattach    => 15,
    WRattach    => 16,
    WMsetname   => 17,
    WRsetname   => 18,
    WMgetname   => 19,
    WRgetname   => 20,
    WMsettools  => 21,
    WRsettools  => 22,
    WMgettools  => 23,
    WRgettools  => 24,
    WMread      => 25,
    WRread      => 26,
    WMreplace   => 27,
    WRreplace   => 28,
    WMexec      => 29,
    WRexec      => 30,
    WMgoto      => 31,
    WRgoto      => 32,
    WMfencepost => 33,

    COOKIE      => 0xFEED,
    HEADER_FMT  => 'nnNnnNNn', # cookie(2), t(2), len(4), m(2), w(2), p0(4), p1(4), flag(2)
    HEADERSIZE  => 22,
};

my @EVENT_CONSTS = qw(WEexec WEgoto WEdestroy WEreplace);
my @RPC_CONSTS   = qw(
    WRerror
    WMlist WRlist
    WMnew WRnew
    WMattach WRattach
    WMsetname WRsetname
    WMgetname WRgetname
    WMsettools WRsettools
    WMgettools WRgettools
    WMread WRread
    WMreplace WRreplace
    WMexec WRexec
    WMgoto WRgoto
);
my @OTHER_CONSTS = qw(WEfencepost WMfencepost);

our @EXPORT_OK = (@EVENT_CONSTS, @RPC_CONSTS, @OTHER_CONSTS, 'get_wily_fifo');
our %EXPORT_TAGS = (
    events => \@EVENT_CONSTS,
    rpc    => \@RPC_CONSTS,
    all    => \@EXPORT_OK,
);

sub get_wily_fifo {
    return $ENV{WILYFIFO} if defined $ENV{WILYFIFO};

    my $user   = (getpwuid($<))[0] // die "Cannot determine username\n";
    my $disp   = $ENV{DISPLAY}     // die "\$DISPLAY not set and \$WILYFIFO not set\n";
    my $tmpdir = $ENV{TMPDIR} || '/tmp';

    return "$tmpdir/wily$user$disp";
}

sub new {
    my ($class, %args) = @_;
    return bless {
        fifo_path => $args{fifo_path} // get_wily_fifo(),
        sock      => undef,
        next_id   => 1,
        events    => [],
    }, $class;
}

sub connect {
    my ($self) = @_;

    my $sock_path = File::Temp::mktemp('/tmp/wily.XXXXXX');
    my $srv = IO::Socket::UNIX->new(
        Type   => SOCK_STREAM,
        Local  => $sock_path,
        Listen => 1,
    ) or die "Cannot create UNIX socket $sock_path: $!\n";

    eval {
        sysopen(my $fifo_fh, $self->{fifo_path}, O_WRONLY)
            or die "Cannot open Wily FIFO $self->{fifo_path}: $!\n";

        my $path_bytes = encode_utf8($sock_path);
        my $written    = syswrite($fifo_fh, $path_bytes);
        close($fifo_fh);

        die "Short write to Wily FIFO\n"
            if !defined($written) || $written != length($path_bytes);

        $self->{sock} = $srv->accept()
            or die "Failed to accept connection from Wily: $!\n";
        binmode($self->{sock});
    };
    my $err = $@;

    close($srv);
    unlink($sock_path) if -e $sock_path;
    die $err if $err;

    return $self;
}

sub close {
    my ($self) = @_;
    if ($self->{sock}) {
        close($self->{sock});
        $self->{sock} = undef;
    }
}

sub DESTROY {
    my ($self) = @_;
    $self->close();
}

sub fileno {
    my ($self) = @_;
    die "Not connected to Wily\n" unless $self->{sock};
    return fileno($self->{sock});
}

sub _alloc_id {
    my ($self) = @_;
    my $mid = $self->{next_id};
    $self->{next_id} = ($self->{next_id} % 0xFFFF) + 1;
    return $mid;
}

sub _pack_msg {
    my ($msg) = @_;
    my $payload   = encode_utf8($msg->{s} // '') . "\x00";
    my $total_len = HEADERSIZE + length($payload);

    my $header = pack(
        HEADER_FMT,
        COOKIE,
        $msg->{t},
        $total_len,
        $msg->{m}    // 0,
        $msg->{w}    // 0,
        $msg->{p0}   // 0,
        $msg->{p1}   // 0,
        $msg->{flag} // 0,
    );
    return $header . $payload;
}

sub _send_all {
    my ($self, $data) = @_;
    die "Not connected to Wily\n" unless $self->{sock};

    my $offset = 0;
    my $len    = length($data);
    while ($offset < $len) {
        my $sent = syswrite($self->{sock}, $data, $len - $offset, $offset);
        die "Socket write error: $!\n" unless defined $sent;
        $offset += $sent;
    }
}

sub _recv_exact {
    my ($self, $n) = @_;
    die "Not connected to Wily\n" unless $self->{sock};

    my $buf = '';
    while (length($buf) < $n) {
        my $chunk = '';
        my $read  = sysread($self->{sock}, $chunk, $n - length($buf));
        die "Socket read error: $!\n" unless defined $read;
        if ($read == 0) {
            $self->close();
            die "Wily closed the IPC connection\n";
        }
        $buf .= $chunk;
    }
    return $buf;
}

sub _recv_raw_msg {
    my ($self) = @_;
    my $header = $self->_recv_exact(HEADERSIZE);
    my ($cookie, $mtype, $total_len, $msg_id, $win_id, $p0, $p1, $flag) =
        unpack(HEADER_FMT, $header);

    if ($cookie != COOKIE || $total_len < HEADERSIZE) {
        die sprintf("Corrupt Wily packet: cookie=0x%x, len=%d\n", $cookie, $total_len);
    }

    my $raw_text = $self->_recv_exact($total_len - HEADERSIZE);
    substr($raw_text, -1, 1, ''); # Strip trailing NUL byte
    my $text = decode_utf8($raw_text);

    return {
        t    => $mtype,
        m    => $msg_id,
        w    => $win_id,
        p0   => $p0,
        p1   => $p1,
        flag => $flag,
        s    => $text,
    };
}

sub _rpc {
    my ($self, %args) = @_;
    die "Not connected to Wily\n" unless $self->{sock};

    my $mid = $self->_alloc_id();
    my $req = {
        t    => $args{req_type},
        m    => $mid,
        w    => $args{win_id} // 0,
        p0   => $args{p0}     // 0,
        p1   => $args{p1}     // 0,
        flag => $args{flag}   // 0,
        s    => $args{text}   // '',
    };

    $self->_send_all(_pack_msg($req));

    # Read until our matching RPC reply arrives; queue any interleaved events
    while (1) {
        my $msg = $self->_recv_raw_msg();
        if ($msg->{t} < WEfencepost) {
            push @{ $self->{events} }, $msg;
            next;
        }
        if ($msg->{m} != $mid) {
            die "Unexpected message id $msg->{m} (expected $mid)\n";
        }
        if ($msg->{t} == WRerror) {
            die "WilyError: $msg->{s}\n";
        }
        if ($msg->{t} != $args{expected_reply}) {
            die "Unexpected reply type $msg->{t} (expected $args{expected_reply})\n";
        }
        return $msg;
    }
}

# --- High-level RPC methods ---

sub list_windows {
    my ($self) = @_;
    my $reply = $self->_rpc(req_type => WMlist, expected_reply => WRlist);

    my @windows;
    for my $line (split /\R/, $reply->{s}) {
        next unless $line =~ /\S/;
        my ($name, $wid) = split /\s+/, $line, 2;
        push @windows, [ int($wid), $name // '' ];
    }
    return @windows;
}

sub new_win {
    my ($self, $name, %opts) = @_;
    my $reply = $self->_rpc(
        req_type       => WMnew,
        expected_reply => WRnew,
        flag           => $opts{backup} ? 1 : 0,
        text           => $name,
    );
    return $reply->{w};
}

sub attach {
    my ($self, $win_id, $mask) = @_;
    $self->_rpc(
        req_type       => WMattach,
        expected_reply => WRattach,
        win_id         => $win_id,
        flag           => $mask,
    );
}

sub setname {
    my ($self, $win_id, $name) = @_;
    $self->_rpc(
        req_type       => WMsetname,
        expected_reply => WRsetname,
        win_id         => $win_id,
        text           => $name,
    );
}

sub getname {
    my ($self, $win_id) = @_;
    return $self->_rpc(
        req_type       => WMgetname,
        expected_reply => WRgetname,
        win_id         => $win_id,
    )->{s};
}

sub settools {
    my ($self, $win_id, $tools) = @_;
    $self->_rpc(
        req_type       => WMsettools,
        expected_reply => WRsettools,
        win_id         => $win_id,
        text           => $tools,
    );
}

sub gettools {
    my ($self, $win_id) = @_;
    return $self->_rpc(
        req_type       => WMgettools,
        expected_reply => WRgettools,
        win_id         => $win_id,
    )->{s};
}

sub read_range {
    my ($self, $win_id, $p0, $p1) = @_;
    return $self->_rpc(
        req_type       => WMread,
        expected_reply => WRread,
        win_id         => $win_id,
        p0             => $p0,
        p1             => $p1,
    )->{s};
}

sub replace {
    my ($self, $win_id, $p0, $p1, $text) = @_;
    $self->_rpc(
        req_type       => WMreplace,
        expected_reply => WRreplace,
        win_id         => $win_id,
        p0             => $p0,
        p1             => $p1,
        text           => $text,
    );
}

sub exec_cmd {
    my ($self, $win_id, $cmd) = @_;
    $self->_rpc(
        req_type       => WMexec,
        expected_reply => WRexec,
        win_id         => $win_id,
        text           => $cmd,
    );
}

sub goto {
    my ($self, $win_id, $target, %opts) = @_;
    my $set_dot = exists $opts{set_dot} ? $opts{set_dot} : 1;
    my $reply = $self->_rpc(
        req_type       => WMgoto,
        expected_reply => WRgoto,
        win_id         => $win_id,
        flag           => $set_dot ? 1 : 0,
        text           => $target,
    );
    return ($reply->{w}, $reply->{p0}, $reply->{p1});
}

# --- Event Queue & Bouncing ---

sub would_block {
    my ($self) = @_;
    return 0 if @{ $self->{events} };
    return 1 unless $self->{sock};

    my $sel = IO::Select->new($self->{sock});
    return scalar($sel->can_read(0)) == 0 ? 1 : 0;
}

sub next_event {
    my ($self) = @_;
    return shift @{ $self->{events} } if @{ $self->{events} };

    while (1) {
        my $msg = $self->_recv_raw_msg();
        return $msg if $msg->{t} < WEfencepost;
    }
}

sub bounce {
    my ($self, $event) = @_;
    if ($event->{t} == WEexec) {
        $self->_rpc(
            req_type       => WMexec,
            expected_reply => WRexec,
            win_id         => $event->{w},
            p0             => $event->{p0},
            p1             => $event->{p1},
            flag           => $event->{flag},
            text           => $event->{s},
        );
    }
    elsif ($event->{t} == WEgoto) {
        $self->_rpc(
            req_type       => WMgoto,
            expected_reply => WRgoto,
            win_id         => $event->{w},
            p0             => $event->{p0},
            p1             => $event->{p1},
            flag           => $event->{flag},
            text           => $event->{s},
        );
    }
}

1;

__END__

=head1 NAME

Wily::Client - Pure-Perl client for Wily's libmsg / RPC IPC protocol

=head1 SYNOPSIS

    use Wily::Client qw(:events);

    my $wily = Wily::Client->new()->connect();

    for my $win ($wily->list_windows()) {
        my ($id, $name) = @$win;
        print "Window $id: $name\n";
    }

    my $wid = $wily->new_win('+PerlConsole', backup => 0);
    $wily->settools($wid, ' Hello Quit ');
    $wily->replace($wid, 0, 0, "Middle-click (B2) 'Hello' or 'Quit' in the tag.\n");
    $wily->attach($wid, WEexec | WEgoto | WEdestroy);

    while (my $ev = $wily->next_event()) {
        if ($ev->{t} == WEdestroy) {
            last;
        }
        elsif ($ev->{t} == WEexec) {
            (my $cmd = $ev->{s}) =~ s/^\s+|\s+$//g;
            if ($cmd eq 'Hello') {
                $wily->replace($wid, 0, 0, "Clicked Hello!\n");
            }
            elsif ($cmd eq 'Quit') {
                $wily->exec_cmd($wid, 'Del');
                last;
            }
            else {
                # Return built-in commands (Cut, Paste, Snarf, Del) to Wily
                $wily->bounce($ev);
            }
        }
        elsif ($ev->{t} == WEgoto) {
            # Return normal B3 searches/file opens to Wily
            $wily->bounce($ev);
        }
    }

    $wily->close();

=head1 DESCRIPTION

C<Wily::Client> implements a pure-Perl 5 client for communicating with the
Wily text editor over its Unix domain socket IPC protocol (C<libmsg> / C<rpc>).
It uses only core Perl modules and requires no external dependencies or C
compilation.

When an asynchronous event arrives from Wily while the client is waiting for
an RPC response, C<Wily::Client> automatically queues the event in FIFO order
so that L</next_event> delivers it without packet loss.

=head1 EXPORTS

Nothing is exported by default. You may import individual constants,
L</get_wily_fifo>, or use the following export tags:

=over 4

=item C<:events>

Exports the event type / bitmask constants:
C<WEexec>, C<WEgoto>, C<WEdestroy>, and C<WEreplace>.

=item C<:rpc>

Exports all RPC request and response message type constants:
C<WRerror>, C<WMlist>, C<WRlist>, C<WMnew>, C<WRnew>, C<WMattach>, C<WRattach>,
C<WMsetname>, C<WRsetname>, C<WMgetname>, C<WRgetname>, C<WMsettools>,
C<WRsettools>, C<WMgettools>, C<WRgettools>, C<WMread>, C<WRread>,
C<WMreplace>, C<WRreplace>, C<WMexec>, C<WRexec>, C<WMgoto>, and C<WRgoto>.

=item C<:all>

Exports everything in C<:events>, C<:rpc>, C<WEfencepost>, C<WMfencepost>,
and C<get_wily_fifo>.

=back

=head1 CONSTRUCTOR & CONNECTION

=head2 new

    my $wily = Wily::Client->new(%args);

Constructs a new client instance without connecting. Accepts an optional
C<fifo_path> parameter; if omitted, L</get_wily_fifo> is used.

=head2 connect

    $wily->connect();

Creates a temporary Unix domain socket in C</tmp/wily.XXXXXX>, writes its path
to Wily's named pipe (FIFO), and accepts Wily's incoming stream connection.
Returns C<$self> for method chaining. Dies on I/O or socket errors.

=head2 close

    $wily->close();

Closes the active Unix domain socket connection if open. Also called
automatically upon object destruction (C<DESTROY>).

=head2 fileno

    my $fd = $wily->fileno();

Returns the underlying socket file descriptor, useful for integrating with
C<select()>, C<IO::Select>, or external event loops.

=head2 get_wily_fifo

    my $path = Wily::Client::get_wily_fifo();

Resolves the path to Wily's FIFO using C<$ENV{WILYFIFO}> if set, or falling
back to C<${TMPDIR:-/tmp}/wily${USER}${DISPLAY}>.

=head1 RPC METHODS

All RPC methods die with a C<WilyError: ...> message if Wily returns a
C<WRerror> packet, or on protocol/socket errors.

=head2 list_windows

    my @windows = $wily->list_windows();

Returns a list of arrayrefs, each containing C<[ $win_id, $name ]>.

=head2 new_win

    my $wid = $wily->new_win($name, backup => 0);

Opens an existing file or creates a new window named C<$name> and returns its
numeric window ID. Set C<< backup => 1 >> to enable Wily backup/undo tracking
for the window.

=head2 attach

    $wily->attach($win_id, WEexec | WEgoto | WEdestroy);

Subscribes the client to events on C<$win_id> matching the bitmask C<$mask>.

=head2 setname

    $wily->setname($win_id, $name);

Sets the path/name displayed in the tag of C<$win_id>.

=head2 getname

    my $name = $wily->getname($win_id);

Returns the current path/name of C<$win_id>.

=head2 settools

    $wily->settools($win_id, $tools);

Sets the user tool string in the tag bar of C<$win_id>.

=head2 gettools

    my $tools = $wily->gettools($win_id);

Returns the current user tool string in the tag bar of C<$win_id>.

=head2 read_range

    my $text = $wily->read_range($win_id, $p0, $p1);

Reads and returns the UTF-8 text in rune range C<[$p0, $p1)> from C<$win_id>.

=head2 replace

    $wily->replace($win_id, $p0, $p1, $text);

Replaces the rune range C<[$p0, $p1)> in C<$win_id> with C<$text>.

=head2 exec_cmd

    $wily->exec_cmd($win_id, $cmd);

Executes C<$cmd> as if middle-clicked (B2) in C<$win_id>.

=head2 goto

    my ($target_wid, $p0, $p1) = $wily->goto($win_id, $target, set_dot => 1);

Searches or jumps to C<$target> as if right-clicked (B3) in C<$win_id>.
Returns a 3-element list C<($target_wid, $p0, $p1)>. Pass C<< set_dot => 0 >>
to query the target location without moving the selection.

=head1 EVENT HANDLING

=head2 next_event

    my $ev = $wily->next_event();

Returns the next queued event hashref, or blocks until Wily sends one.
Each event hashref contains:

=over 4

=item * C<t> - Event type constant (C<WEexec>, C<WEgoto>, C<WEdestroy>, or C<WEreplace>)

=item * C<m> - Message ID

=item * C<w> - Window ID where the event occurred

=item * C<p0> - Start rune offset

=item * C<p1> - End rune offset

=item * C<flag> - Event flags

=item * C<s> - Event payload string (command text, search target, or replacement text)

=back

=head2 would_block

    if (!$wily->would_block()) {
        my $ev = $wily->next_event();
    }

Returns true (C<1>) if calling L</next_event> would block waiting for data
from Wily, or false (C<0>) if an event is already queued or readable on the
socket.

=head2 bounce

    $wily->bounce($ev);

Returns an unhandled C<WEexec> or C<WEgoto> event back to Wily so that
built-in commands (such as C<Cut>, C<Paste>, C<Snarf>, C<Del>) or standard
B3 searches/file opens are handled normally by Wily.

=head1 SEE ALSO

L<wily(1)>, L<IO::Socket::UNIX>

=head1 LICENSE

This library is free software; you can redistribute it and/or modify it under
the same terms as Perl itself.

=cut
