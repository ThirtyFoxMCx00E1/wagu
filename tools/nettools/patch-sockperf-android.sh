#!/usr/bin/env bash
# 4.05.8: sockperf for Android (Termux / adb shell). Bionic has no backtrace() before API 33: make it optional.
set -e
cd "${1:-.}"
sed -i 's/AC_SEARCH_LIBS(\[backtrace\], \[execinfo\], \[\], AC_MSG_ERROR(\[libexecinfo not found\]))/AC_SEARCH_LIBS([backtrace], [execinfo], [], [])/' configure.ac
sed -i 's|#include <execinfo.h> // for backtraces|#if !defined(__ANDROID__)\n#include <execinfo.h> // for backtraces\n#endif|' src/os_abstract.h
python3 - <<'P'
p='src/os_abstract.cpp'; s=open(p).read()
a='    char **strings;\n    void *m_backtrace[25];'
if '__ANDROID__' not in s.split(a)[0][-200:]:
    i=s.index(a); j=s.index('\n}', i)
    s=s[:i]+'#if !defined(__ANDROID__)\n'+s[i:j]+'\n#endif'+s[j:]
open(p,'w').write(s)
P
# no pthread_setaffinity_np in bionic: the calling thread's affinity via sched_setaffinity(0)
sed -i 's|    if (0 != pthread_setaffinity_np(pthread_self(), sizeof(os_cpuset_t), &(_mycpuset.cpuset)))|#if defined(__ANDROID__)\n    if (0 != sched_setaffinity(0, sizeof(os_cpuset_t), \&(_mycpuset.cpuset)))\n#else\n    if (0 != pthread_setaffinity_np(pthread_self(), sizeof(os_cpuset_t), \&(_mycpuset.cpuset)))\n#endif|' src/os_abstract.cpp
# glibc constant missing from bionic (Linux value)
sed -i '0,/^#include/s//#ifndef IP_MAX_MEMBERSHIPS\n#define IP_MAX_MEMBERSHIPS 20\n#endif\n#include/' src/defs.cpp
