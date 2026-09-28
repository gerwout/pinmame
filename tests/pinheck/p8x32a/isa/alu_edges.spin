PUB main
  cognew(@entry, 0)
  repeat until long[$6FFC]
DAT
        long    $C0DE5EED
        org     0
entry
        test    zero, zero wz, wc
        mov     a, m1
        shl     a, #0 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        shl     a, #31 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        shr     a, #31 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        sar     a, #31 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        ror     a, #1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        rol     a, #1 wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        rcl     a, #4 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        rcl     a, #4 wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        rcr     a, #4 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        rev     a, #0 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        rev     a, #16 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        abs     a, hi wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        neg     a, hi wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        absneg  a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        negc    a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        negz    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        mins    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        maxs    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        min     a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        max     a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        min     a, #0 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        add     a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, mx
        adds    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        sub     a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        subs    a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, m1
        addx    a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        subx    a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        cmpsx   a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, mx
        addsx   a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, hi
        subsx   a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        sumc    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        sumnc   a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, hi
        sumz    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        addabs  a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        subabs  a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        cmpsub  a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        cmpsub  a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        cmps    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        movs    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        movd    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        movi    a, m1 wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        muxc    a, hi wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, m1
        muxnz   a, hi wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        xor     a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        andn    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, mx
        test    a, m1 wz, wc
        call    #put
        wrlong  one, done
        cogid   a
        cogstop a
put     wrlong  a, ptr
        add     ptr, #4
        mov     f, #0
        muxc    f, #1
        muxz    f, #2
        wrlong  f, ptr
        add     ptr, #4
put_ret ret
a       long    0
f       long    0
zero    long    0
one     long    1
m1      long    $FFFFFFFF
hi      long    $80000000
mx      long    $7FFFFFFF
ptr     long    $6000
done    long    $6FFC
        long    $C0DEE0D0
