# Android compatibility helpers

Small source snapshot from
[soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate),
revision `0a382f0ebce348c8cdc68f32fb5fa3186baddc38`:

- `pthread.h`: Android thread-control layouts and the used adapter declarations.
  The adapted implementation lives in `src/compat/bionic_threads.c`; it serializes
  lazy mutex/condition initialization and waits for completed `pthread_once` initialization.
- `fcntl.h`: only the Android-to-newlib `open()` flag conversion.
- `ctype.c`: the original Bionic character tables, unchanged.

The pthread and flag helpers retain their MIT notices. See [LICENSE](LICENSE).
The character tables carry J.T. Conklin's original public-domain notice.
