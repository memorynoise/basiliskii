/* acconfig.h
   This file is in the public domain.

   Descriptive text for the C preprocessor macros that
   the distributed Autoconf macros can define.
   No software package will use all of them; autoheader copies the ones
   your configure.in uses into your configuration header file templates.

   The entries are in sort -df order: alphabetical, case insensitive,
   ignoring punctuation (such as underscores).  Although this order
   can split up related entries, it makes it easier to check whether
   a given entry is in the file.

   Leave the following blank line there!!  Autoheader needs it.  */


/* Define to 'off_t' if <sys/types.h> doesn't define. */
#undef loff_t

/* Define to 'int' if <sys/types.h> doesn't define. */
#undef socklen_t

/* Define if using ESD. */
#undef ENABLE_ESD

/* Define if using DGA with framebuffer device. */
#undef ENABLE_FBDEV_DGA

/* Define if using GTK. */
#undef ENABLE_GTK

/* Define if using "mon". */
#undef ENABLE_MON

/* Define if using native 68k mode. */
#undef ENABLE_NATIVE_M68K

/* Define if using XFree86 DGA extension. */
#undef ENABLE_XF86_DGA

/* Define if using XFree86 VidMode extension. */
#undef ENABLE_XF86_VIDMODE

/* Define if pthreads are available. */
#undef HAVE_PTHREADS

/* Define if using video enabled on SEGV signals */
#undef ENABLE_VOSF

/* Define if your system requires signals to be reinstalled */
#undef SIGNAL_NEED_REINSTALL

/* Define if your system requires sigactions to be reinstalled */
#undef SIGACTION_NEED_REINSTALL

/* Define if your system support extended signals */
#undef HAVE_SIGINFO_T

/* Define if we know a hack to replace siginfo_t::si_addr member */
#undef HAVE_SIGCONTEXT_SUBTERFUGE

/* Define if your system has a working vm_allocate()-based memory allocator */
#undef HAVE_MACH_VM

/* Define if your system has a working mmap()-based memory allocator */
#undef HAVE_MMAP_VM

/* Define if <sys/mman.h> defines MAP_ANON and mmap()'ing with MAP_ANON works */
#undef HAVE_MMAP_ANON

/* Define if <sys/mman.h> defines MAP_ANONYMOUS and mmap()'ing with MAP_ANONYMOUS works */
#undef HAVE_MMAP_ANONYMOUS

/* Define if libgnomeui is available */
#undef HAVE_GNOMEUI


/* Leave that blank line there!!  Autoheader needs it.
   If you're adding to this file, keep in mind:
   The entries are in sort -df order: alphabetical, case insensitive,
   ignoring punctuation (such as underscores).  */
