# GomSpace full client

This project stages the full `csp-client` binary as `gs-client`. The vendor
`gs-client.zip` in the Yocto tree is password protected, so this project uses
the existing ARM binary from the EXPv110 image. It does not rebuild the full
client from source. The binary requires `libzmq.so.5` and `libsocketcan.so.2`
on the target.

```sh
make -C service/gs-client check
make -C service/gs-client deploy
```

`deploy` stages `service/deploy/gs-client/csp-client`. The board installer puts
it in `/home/root/tools/bin/`. For EXP RPARAM table reads, use the corrected
`lite-client` project; this full client predates that fragment fix.
