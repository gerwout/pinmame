' EXPECT-JITVAR: 0
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0 runs program A in cog 1 (it rewrites the opcode of slot 1 after running it), then program B in cog 1 at the
' same cog addresses (slot 1 in a loop); B's sum at $6000
entry   mov     x, wk
        shl     x, #2
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d30000
        waitcnt t, #0
        mov     x, wk
        add     x, #32
        shl     x, #2
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d60000
        waitcnt t, #0
        cogid   x
        cogstop x
wk      long    $C0DEADD1
d30000  long    30000
d60000  long    60000
x       long    0
t       long    0
        long    $C0DEE0D0
        long    $C0DE0B0B
' program A, 8 longs
        org     0
pa      mov     k, #50
pax     add     acc, #1
        djnz    k, #pax
        movi    pax, #%011000_001
        cogid   k
        cogstop k
k       long    0
acc     long    0
' program B
        org     0
pb      mov     k2, d2000
pbx     add     acc2, #3
        djnz    k2, #pbx
        wrlong  acc2, rb
        cogid   k2
        cogstop k2
d2000   long    2000
rb      long    $6000
k2      long    0
acc2    long    0
