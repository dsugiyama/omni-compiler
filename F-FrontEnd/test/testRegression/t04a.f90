      program main
        external func
        integer res
        res = func(1., 2., 3.)
        call sub(4., 5.)
      end program

      subroutine sub(x, y)
        real x, y
      end subroutine

      function func (a, b, c)
        real a, b, c, func
        func = a + b + c
      end function
