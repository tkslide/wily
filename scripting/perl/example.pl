#!/usr/bin/env perl
use strict;
use warnings;
use lib 'lib';
use Wily::Client;

my $wily = Wily::Client->new()->connect();

my @wins = $wily->list_windows();
print "Existing windows:\n";
print "  $_->[0]: $_->[1]\n" for @wins;

my $wid = $wily->new_win('+PerlConsole', backup => 0);
$wily->settools($wid, ' Hello Quit ');
$wily->replace($wid, 0, 0, "Middle-click (B2) 'Hello' or 'Quit' in the tag.\n");
$wily->attach(
    $wid,
    Wily::Client::WEexec | Wily::Client::WEgoto | Wily::Client::WEdestroy
);

while (1) {
    my $ev = $wily->next_event();
    if ($ev->{t} == Wily::Client::WEdestroy) {
        print "Window closed, exiting.\n";
        last;
    }
    elsif ($ev->{t} == Wily::Client::WEexec) {
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
    elsif ($ev->{t} == Wily::Client::WEgoto) {
        # Let Wily handle normal B3 searches/file opens
        $wily->bounce($ev);
    }
}

$wily->close();
