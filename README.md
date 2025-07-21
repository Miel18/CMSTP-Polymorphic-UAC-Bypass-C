# CMSTP-Polymorphic-UAC-Bypass-C
Native C PoC for a polymorphic UAC bypass using cmstp.exe (MITRE T1218.003, T1548.003). Dynamically generates a unique INF file, automates elevation, and cleans all artifacts. Includes anti-analysis delay and debug mode. For educational and red team research only.

# To compile
gcc -Wall -Wextra -std=c99 -O2 main.c cmstp_bypass.c -o cmstp_demo.exe -lkernel32 -luser32 -ladvapi32 -lshell32 -lshlwapi
