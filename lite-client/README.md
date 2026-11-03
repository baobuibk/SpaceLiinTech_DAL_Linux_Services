# DAL lite-client

This project contains the EXP reference Linux CSP client source. It builds
`csp-client-lite` and stages the `lite-client` shell wrapper. The RPARAM full
table reader handles the EXP fragment count correctly, including tables with
more than one fragment.

```sh
make -C service/lite-client check
make -C service/lite-client deploy
```

The target build uses `SDK_ROOT` (default `/home/buildmachine/oecore-obcv110`).
`deploy` stages both files in `service/deploy/lite-client/`. On the board,
`make -C /home/root/deploy install` installs them to `/home/root/tools/bin/`.

Example: `/home/root/tools/bin/lite-client -c=can0 getall 11 2`.
