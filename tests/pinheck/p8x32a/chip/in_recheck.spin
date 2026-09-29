' ARGS: -extat 10250 80 -extat 12251 0 -extat 14252 80 -extat 16253 0 -extat 18254 80 -extat 20255 0 -extat 22256 80 -extat 24257 0 -extat 26258 80 -extat 28259 0 -extat 30260 80 -extat 32261 0 -extat 34262 80 -extat 36263 0 -extat 38264 80 -extat 40265 0 -extat 42266 80 -extat 44267 0 -extat 46268 80 -extat 48269 0 -extat 50270 80 -extat 52271 0 -extat 54272 80 -extat 56273 0 -extat 58274 80 -extat 60275 0 -extat 62276 80 -extat 64277 0 -extat 66278 80 -extat 68279 0 -extat 70280 80 -extat 72281 0
' EXPECT-SLEEPS: 20
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     w, cnt
        add     w, d2000
        mov     k, #32
:next   waitcnt w, d2000
        shr     pat, #1 wc
  if_c  jmp     #:hi
:lo     test    p7, ina wz
        rdlong  x, scr
  if_nz jmp     #:lo
        jmp     #:rec
:hi     test    p7, ina wz
        rdlong  x, scr
  if_z  jmp     #:hi
:rec    mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        djnz    k, #:next
        cogid   t
        cogstop t
p7      long    %10000000
pat     long    $5555_5555
d2000   long    2000
scr     long    $6200
k       long    0
x       long    0
t       long    0
w       long    0
ptr     long    $6000
        long    $C0DEE0D0
