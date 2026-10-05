#ifndef UIIPFSTOOL_HPP
#define UIIPFSTOOL_HPP

struct sockaddr_in;
// Reads "ip", "ip:port" or "ip:port c=N" from a file on the disc. N (0-9) is
// the player's colour and is written to `color` when present.
bool readIpAddrFs(const char *path, sockaddr_in *addr, unsigned char *color = 0);

#endif
