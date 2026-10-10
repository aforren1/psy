/* Compile check: ysp/net.h's declarations of liblsl's functions against
 * liblsl's own lsl_c.h (the copy tools/vendor_lsl.py fetches). Built only
 * when that copy is there (CMakeLists.txt). Each function's type in the
 * header's table must equal lsl_c.h's after two allowed substitutions: any
 * pointer is void* (the table keeps LSL's handle types opaque) and an enum
 * is int (lsl_channel_format_t; its 0x7f000000 member makes it int-sized).
 * decltype does not evaluate its operand, so nothing is linked; running it
 * only returns 0. */
#define YSP_NET_IMPLEMENTATION
#include "ysp/net.h"
/* declarations only: no dllimport, no #pragma comment(lib), nothing to link */
#define LIBLSL_FFI
#include <lsl_c.h>

#include <type_traits>

template <class T, bool E = std::is_enum<T>::value> struct ynet_norm { typedef T type; };
template <class T> struct ynet_norm<T, true> { typedef int type; };
template <class T> struct ynet_norm<T*, false> { typedef void* type; };
template <class T> struct ynet_norm<T* const, false> { typedef void* type; };

template <class F> struct ynet_sig;
template <class R, class... A> struct ynet_sig<R (*)(A...)> {
    typedef typename ynet_norm<R>::type (*type)(typename ynet_norm<A>::type...);
};

#define YNET_ABI(f)                                                                                  \
    static_assert(std::is_same<ynet_sig<decltype(&::lsl_##f)>::type,                                \
                               ynet_sig<decltype(ynet_lsl::f)>::type>::value,                        \
                  "ysp/net.h declares lsl_" #f " unlike lsl_c.h")

YNET_ABI(library_version);
YNET_ABI(local_clock);
YNET_ABI(destroy_string);
YNET_ABI(create_streaminfo);
YNET_ABI(destroy_streaminfo);
YNET_ABI(get_name);
YNET_ABI(get_type);
YNET_ABI(get_source_id);
YNET_ABI(get_hostname);
YNET_ABI(get_uid);
YNET_ABI(get_channel_count);
YNET_ABI(get_nominal_srate);
YNET_ABI(get_channel_format);
YNET_ABI(resolve_bypred);
YNET_ABI(resolve_all);
YNET_ABI(create_outlet);
YNET_ABI(destroy_outlet);
YNET_ABI(push_sample_strtp);
YNET_ABI(push_sample_itp);
YNET_ABI(push_chunk_ftnp);
YNET_ABI(have_consumers);
YNET_ABI(create_inlet);
YNET_ABI(destroy_inlet);
YNET_ABI(open_stream);
YNET_ABI(time_correction_ex);
YNET_ABI(pull_sample_str);
YNET_ABI(pull_sample_i);
YNET_ABI(pull_chunk_f);

/* The constants this header copies from lsl_c.h. */
static_assert(YNET_FLOAT32 == cft_float32 && YNET_STRING == cft_string && YNET_INT32 == cft_int32,
              "channel formats");
static_assert(lsl_lost_error == -2, "ysp/net.h tests ec == -2 for a lost stream");

int main() { return 0; }
