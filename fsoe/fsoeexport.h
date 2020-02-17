
#ifndef FSOE_EXPORT_H
#define FSOE_EXPORT_H

#ifdef FSOE_STATIC_DEFINE
#  define FSOE_EXPORT
#  define FSOE_NO_EXPORT
#else
#  ifndef FSOE_EXPORT
#    ifdef fsoe_EXPORTS
        /* We are building this library */
#      define FSOE_EXPORT 
#    else
        /* We are using this library */
#      define FSOE_EXPORT 
#    endif
#  endif

#  ifndef FSOE_NO_EXPORT
#    define FSOE_NO_EXPORT 
#  endif
#endif

#ifndef FSOE_DEPRECATED
#  define FSOE_DEPRECATED 
#endif

#ifndef FSOE_DEPRECATED_EXPORT
#  define FSOE_DEPRECATED_EXPORT FSOE_EXPORT FSOE_DEPRECATED
#endif

#ifndef FSOE_DEPRECATED_NO_EXPORT
#  define FSOE_DEPRECATED_NO_EXPORT FSOE_NO_EXPORT FSOE_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef FSOE_NO_DEPRECATED
#    define FSOE_NO_DEPRECATED
#  endif
#endif

#endif /* FSOE_EXPORT_H */
