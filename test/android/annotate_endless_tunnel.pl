#!/usr/bin/env perl
use strict;
use warnings;

# Only annotate the game's own sources, after includes, so SDK/GLM stay intact.
my $dir = shift or die "usage: $0 generated-source-directory\n";
for my $file (glob("$dir/*.cpp"), glob("$dir/*.hpp")) {
  open my $input, '<', $file or die "$file: $!";
  local $/;
  my $text = <$input>;
  close $input;
  my $start = 0;
  while ($text =~ /^\s*#include[^\n]*\n/gm) { $start = pos($text); }
  if (!$start && $text =~ /^#define\s+endlesstunnel_\w+[^\n]*\n/m) {
    $start = $+[0];
  }
  die "no safe annotation position: $file\n" unless $start;
  my $push = "\n#ifdef VLLVM_TEST_ANNOTATION\n#pragma clang attribute push (__attribute__((annotate(VLLVM_TEST_ANNOTATION))), apply_to = function)\n#endif\n";
  my $pop = "\n#ifdef VLLVM_TEST_ANNOTATION\n#pragma clang attribute pop\n#endif\n";
  if ($file =~ /\.hpp$/) {
    $text =~ s/(#endif[^\n]*\s*)$/$pop$1/ or die "no header guard: $file\n";
  } else {
    $text .= $pop;
  }
  substr($text, $start, 0, $push);
  open my $output, '>', $file or die "$file: $!";
  print {$output} $text;
  close $output;
}
