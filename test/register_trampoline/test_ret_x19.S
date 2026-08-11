.text
.globl _my_call0
.p2align 2
_my_call0:
  adrp x19, _target_value@PAGE
  add x19, x19, _target_value@PAGEOFF
  ret x19
