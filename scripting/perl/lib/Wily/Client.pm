package Wily::Client;

use strict;
use warnings;
use utf8;
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

sub get_wily_fifo {
    return $ENV{WILYFIFO} if defined $ENV{WILYFIFO};

    my $user = (getpwuid($<))[0] // die "Cannot determine username\n";
    my $disp = $ENV{DISPLAY}     // die "\$DISPLAY not set and \$WILYFIFO not set\n";
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
        my $written = syswrite($fifo_fh, $path_bytes);
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
