#!/usr/bin/perl
# P122: C_COMPILER_LAUNCHER for the PSP's game and host-core code: float
# arithmetic at run time in the FPU's rounding mode (FCSR: toward zero on the
# game threads, as the EE), never folded to nearest by the optimiser.
#   psp-fp-cc.pl COMPILER [ARGS...] -o OBJ -c SRC
# 1. the same command, front end only (-Xclang -disable-llvm-passes): LLVM IR
#    with what the C front end folds itself (literals, constant operands, as
#    every port and the EE's compiler fold them), nothing folded after;
#    -ffp-contract=off (no fused multiply-add); it also writes the -MD file,
# 2. float fadd/fsub/fmul/fdiv, sitofp/uitofp, fptrunc and sqrt become
#    constrained intrinsics with dynamic rounding, the module strictfp,
# 3. the IR is optimised and compiled with the original flags; the Mips
#    backend lowers the constrained nodes to the usual FPU instructions.
# Same split as port/cmake/ps2fp-cc.pl (the other ports, ps2_* helpers);
# here the FPU rounds itself (port/psp/host/psp_fpu.c).  -ffp-model=strict
# alone is not enough: it also defers the front end's own conversions of
# literals ((float)4.1666667908e-02 in newlib's cosf) to run time.
# MVC2_NO_PSPFP=1 in the build environment compiles as usual (A/B checks).
use strict;
use warnings;

my @cmd = @ARGV;
my ($obj, $src);
for my $i (0 .. $#cmd) {
    $obj = $cmd[$i + 1] if $cmd[$i] eq '-o' && $i < $#cmd;
    $src = $cmd[$i] if $cmd[$i] =~ /\.c$/;
}
if ($ENV{MVC2_NO_PSPFP} || !$obj || !$src || $src =~ /ps2_float\.c$/) {
    exec @cmd or die "psp-fp-cc: exec $cmd[0]: $!";
}
my ($ir1, $ir2) = ("$obj.pspfp.ll", "$obj.pspfp.r.ll");
my @c1;
for (my $i = 0; $i <= $#cmd; $i++) {
    my $a = $cmd[$i];
    if ($a eq '-o') { push @c1, '-o', $ir1; $i++; next; }
    push @c1, $a;
}
push @c1, '-S', '-emit-llvm', '-Xclang', '-disable-llvm-passes', '-ffp-contract=off';
system(@c1) == 0 or exit 1;
rewrite($ir1, $ir2);
unlink $ir1;
my @c2;
for (my $i = 0; $i <= $#cmd; $i++) {
    my $a = $cmd[$i];
    if ($a eq $src || $a eq '-c' || $a eq '-MD' || $a eq '-MMD') { next; }
    if ($a eq '-MT' || $a eq '-MF' || $a eq '-include' || $a eq '-I' || $a eq '-D') { $i++; next; }
    if ($a =~ /^-(I|D|std=|include)/) { next; }
    push @c2, $a;
}
push @c2, '-Wno-unused-command-line-argument', '-c', '-x', 'ir', $ir2;
my $rc = system(@c2);
unlink $ir2;
exit($rc == 0 ? 0 : 1);

sub rewrite {
    my ($in, $out) = @_;
    open(my $fh, '<', $in) or die "psp-fp-cc: $in: $!";
    my @lines = <$fh>;
    close $fh;
    my $md = 'metadata !"round.dynamic", metadata !"fpexcept.ignore"';
    my ($maxattr, %decl) = (0);
    for (@lines) { $maxattr = $1 if /^attributes #(\d+) / && $1 > $maxattr; }
    my $sa = '#' . ($maxattr + 1);   # call-site strictfp
    for (@lines) {
        my $lhs = /^(\s*%[-\w.]+ = )/ ? $1 : undef;
        if (defined $lhs && /^\s*%[-\w.]+ = (fadd|fsub|fmul|fdiv)((?: [a-z]+)*) float ([^,]+), ([^,\n]+)(.*)$/) {
            my ($op, $a, $b, $rest) = ($1, $3, $4, $5);
            my $f = "llvm.experimental.constrained.$op.f32";
            $decl{$f} = "declare float \@$f(float, float, metadata, metadata)";
            $_ = "${lhs}call float \@$f(float $a, float $b, $md) $sa$rest\n";
            next;
        }
        if (/^\s*%[-\w.]+ = (fadd|fsub|fmul|fdiv|frem)\b.*\bfloat\b/) {
            die "psp-fp-cc: $in: float arithmetic not handled: $_";
        }
        if (defined $lhs && /^\s*%[-\w.]+ = (sitofp|uitofp) (i\d+) ([^ ]+) to float(.*)$/) {
            my ($op, $ty, $v, $rest) = ($1, $2, $3, $4);
            my $f = "llvm.experimental.constrained.$op.f32." . $ty;
            $decl{$f} = "declare float \@$f($ty, metadata, metadata)";
            $_ = "${lhs}call float \@$f($ty $v, $md) $sa$rest\n";
            next;
        }
        if (defined $lhs && /^\s*%[-\w.]+ = fptrunc double ([^ ]+) to float(.*)$/) {
            my ($v, $rest) = ($1, $2);
            my $f = 'llvm.experimental.constrained.fptrunc.f32.f64';
            $decl{$f} = "declare float \@$f(double, metadata, metadata)";
            $_ = "${lhs}call float \@$f(double $v, $md) $sa$rest\n";
            next;
        }
        if (defined $lhs && /^\s*%[-\w.]+ = (?:tail )?call (?:[a-z]+ )*float \@(?:llvm\.sqrt\.f32|sqrtf)\(float ([^)]+)\)(.*)$/) {
            my ($v, $rest) = ($1, $2);
            $rest =~ s/^\s*#\d+//;
            my $f = 'llvm.experimental.constrained.sqrt.f32';
            $decl{$f} = "declare float \@$f(float, metadata, metadata)";
            $_ = "${lhs}call float \@$f(float $v, $md) $sa$rest\n";
            next;
        }
        if (/\@llvm\.(fmuladd|fma)\.f32/) {
            die "psp-fp-cc: $in: fused multiply-add (needs -ffp-contract=off): $_";
        }
        # every function of the module strictfp (constrained operations, and
        # inlining between them)
        s/^(attributes #\d+ = \{ )/$1strictfp /;
    }
    my $text = join('', @lines);
    # definitions without an attribute group
    $text =~ s/^(define [^\n{]*\))( \{)/$1 $sa$2/mg;
    for my $f (sort keys %decl) {
        next if $text =~ /^declare [^\n]*\@\Q$f\E\(/m;
        $text .= "\n$decl{$f}\n";
    }
    $text .= "\nattributes $sa = { strictfp }\n";
    open(my $oh, '>', $out) or die "psp-fp-cc: $out: $!";
    print $oh $text;
    close $oh;
}
