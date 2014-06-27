Omni Compiler Software RELEASE NOTES

# ver. 0.8.0      2014/04/25
* Support Coarray communication for the K computer.
* Support the gblock distribution
* Support the template_fix directive
* Support the array directive (with some restrictions)
* Support clauses of the reflect directive in XMP/C
* Support the xmp_malloc built-in function in XMP/C
* Support clauses of the bcast, barrier, reduction directives in XMP/F
* Support fully the node directive in XMP/F
* Fixed lots of bugs.

# ver. 0.7.0      2013/11/20
* Improve shadow/reflect directive.
* Fixed some bugs.

# ver. 0.6.1      2013/03/27
* Supports stride Coarray function. (only by using GASNet)
* Supports coarray function on the K computer.

# ver. 0.6.0      2012/11/12
* Initial release of XcalableMP Fortran compiler.
* (Coarray feature will be added soon)

# ver. 0.5.4      2011/11/11
* Supports block-cyclic distribution in distribute directive

# ver. 0.5.3      2011/04/08
* Fixed problems with rewriting expressions

# ver. 0.5.2      2011/03/11
* Fixed bugs in runtime

# ver. 0.5.1      2011/03/09
* gmove between aligned array and duplicated array using array section is suppoted.

--
    #pragma xmp align a[i] with t(i)

    #pragma xmp gmove
    local[:] = a[:];
--

* full shadow reflect is implemented.

# ver. 0.5.0	2010/11/13
* This is the first release of Omni Compiler.