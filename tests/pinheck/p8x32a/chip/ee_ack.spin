' ARGS: -eeprom /dev/null
' EXPECT-SLEEPS: 7
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     y, #$61
        shl     y, #24
        or      x, y
        coginit x
        mov     k, #7
:next   shr     pat, #1 wc
  if_c  jmp     #:hi
:lo     test    sda, ina wz
  if_nz jmp     #:lo
        jmp     #:rec
:hi     test    sda, ina wz
  if_z  jmp     #:hi
:rec    mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        djnz    k, #:next
        cogid   t
        cogstop t
sda     long    1 << 29
pat     long    %0101010
k       long    0
t       long    0
x       long    0
y       long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     w, cnt
        add     w, d2000
        waitcnt w, d2000
        or      dira, sdaw
        waitcnt w, d2000
        or      dira, scl
        mov     n, #9
:bit    shl     b, #1 wc
        muxnc   dira, sdaw
        waitcnt w, d2000
        andn    dira, scl
        waitcnt w, d2000
        or      dira, scl
        waitcnt w, d2000
        djnz    n, #:bit
        cogid   w
        cogstop w
sdaw    long    1 << 29
scl     long    1 << 28
b       long    $A1FF_FFFF
d2000   long    2000
w       long    0
n       long    0
        long    $C0DEE0D0
