module m_nested_common_name_3_p
  integer a0[*]
end module
module m_nested_common_name_3
use m_nested_common_name_3_p
contains
  subroutine sub1(v)
    integer, save :: a0[*]
    integer v
    a0 = v
  end subroutine
end module

use m_nested_common_name_3

a0 = 10
call sub1(100)
if(a0.eq.10) then
  print *, 'OK 1'
else
  print *, 'NG 1 : a0 = ', a0
end if
call sub2

contains
  subroutine sub2
    if(a0.eq.10) then
      print *, 'OK 2'
    else
      print *, 'NG 2 : a0 = ', a0
    end if
  end subroutine
end
