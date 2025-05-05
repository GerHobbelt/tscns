
#pragma once

#if defined(BUILD_MONOLITHIC)

#ifdef __cplusplus
extern "C" {
#endif

int tscns_test_main(int argc, const char** argv);
int tscns_alt_test_main(int argc, const char** argv);

#ifdef __cplusplus
}
#endif

#endif
