Before webOS 5, webOS incorrectly shipped libcurl to `libcurl.so.5`,
instead of `libcurl.so.4`. (See [this link][oe-libcurl] for more info)

This causes great pain targeting different system versions.

With this library, you can link to curl normally, and ship this library with your application.

On webOS 4 and below, it will resolve `libcurl.so.5` for you.

[oe-libcurl]: https://www.openembedded.org/pipermail/openembedded-core/2017-February/132583.html