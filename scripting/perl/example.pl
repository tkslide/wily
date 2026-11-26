#!/usr/bin/env perl
use strict;
use warnings;
use utf8;
use FindBin qw($Bin);
use lib "$Bin/lib";

use Wily::Client qw(:events);

my $wily = Wily::Client->new()->connect();

my @wins = $wily->list_windows();
print "Existing windows:\n";
for my $win (@wins) {
    print "  $win->[0]: $win->[1]\n";
}

my $wid = $wily->new_win('+PerlConsole', backup => 0);
$wily->settools($wid, ' Hello Quit ');
$wily->replace($wid, 0, 0, "Middle-click (B2) 'Hello' or 'Quit' in the tag.\n");
$wily->attach($wid, WEexec | WEgoto | WEdestroy);

while (1) {
    my $ev = $wily->next_event();
    if ($ev->{t} == WEdestroy) {
        print "Window closed, exiting.\n";
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
            # Let Wily handle built-in commands like Cut, Paste, Snarf, Del
            $wily->bounce($ev);
        }
    }
    elsif ($ev->{t} == WEgoto) {
        # Let Wily handle normal B3 searches/file opens
        $wily->bounce($ev);
    }
}

$wily->close();
