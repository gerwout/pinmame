PUB main
DAT
        long    $C0DE5EED
        org     0
' a hub read that ends a translated block loads the instruction of a later block's slot (an ADD and a SUB in turn):
' that block is translated again; the sum at $6000
entry   mov     p, tbl
        mov     k, #64
fill    wrlong  i1, p
        add     p, #4
        wrlong  i2, p
        add     p, #4
        djnz    k, #fill
        mov     p, tbl
        mov     k, #128
lp      rdlong  ins, p
        add     p, #4
        xor     junk, #1
ins     xor     junk, #2
        rol     acc, #1
        djnz    k, #lp
        wrlong  acc, r0
        cogid   k
        cogstop k
i1      add     acc, #1
i2      sub     acc, #3
tbl     long    $6100
r0      long    $6000
p       long    0
k       long    0
acc     long    1
junk    long    0
        long    $C0DEE0D0
