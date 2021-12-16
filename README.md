# gdk-pixbuf-loader-lunasvg

This is a basic GdkPixbuf SVG loader that can be used in place of the one
provided by `librsvg`.

It uses https://github.com/sammycage/lunasvg for the SVG rendering. The code
is not the best as it does not properly support incremental loading and the
likes, but it works satisfactorily for all the icon themes I have tried it
with.

By default, it will fetch and build its own LunaSVG which will be statically
linked into the module. This is the recommended way to do it as LunaSVG does
not appear to properly version its `SONAME` or handle symbol visibility.

To prevent it from fetching a copy, just drop your own into the `subprojects`
directory.

Note that it replaces/conflicts with the loader provided by `librsvg`. This
is intentional.

