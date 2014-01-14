program tgmove
integer :: i
integer,parameter :: n=8
integer a(n),b(n)
integer xmp_node_num
!$xmp nodes p(2)
!$xmp template tx(n)
!$xmp distribute tx(cyclic(2)) onto p
!$xmp align a(i) with tx(i)

!$xmp loop (i) on tx(i)
do i=1,n
  a(i)=i
end do

do i=1,n
  b(i)=0
end do

!$xmp gmove
b(2:5)=a(5:8)

ierr=0
do i=2,5
  ierr=ierr+abs(b(i)-(i+3))
end do

!$xmp reduction (max:ierr)
irank=xmp_node_num()
if (irank==1) then
  print *, 'max error=',ierr
endif
!call exit(ierr)

stop
end
