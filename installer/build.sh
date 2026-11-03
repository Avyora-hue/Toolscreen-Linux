#!/bin/sh
# Builds the installer jar (with the native library embedded) into ../dist/.
# Needs native/build/libtoolscreen.so (run ../native/build.sh first) and Python 3.
# Uses javac when a JDK is installed; otherwise downloads the Eclipse compiler (ECJ) into ../.tools/.
set -e
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
VERSION=1.3.6
OUT="$ROOT/dist/Toolscreen-$VERSION-linux-double-click-me.jar"
rm -rf build && mkdir -p build/classes/native build/classes/META-INF
if command -v javac >/dev/null 2>&1; then
    javac --release 8 -nowarn -encoding UTF-8 -d build/classes src/com/toolscreen/linux/Installer.java
else
    ECJ="$ROOT/.tools/ecj.jar"
    mkdir -p "$ROOT/.tools"
    [ -f "$ECJ" ] || curl -fL -o "$ECJ" https://repo1.maven.org/maven2/org/eclipse/jdt/ecj/3.33.0/ecj-3.33.0.jar
    # ECJ reads the class library from a JDK-style folder; some distros' JREs lack the 'release' file it wants.
    JAVA_BIN=$(readlink -f "$(command -v java)")
    JHOME=$(dirname "$(dirname "$JAVA_BIN")")
    SYS="$ROOT/.tools/jre-system"
    mkdir -p "$SYS"
    ln -sfn "$JHOME/lib" "$SYS/lib"
    [ -f "$SYS/release" ] || printf 'JAVA_VERSION="%s"\n' "$(java -XshowSettings:properties -version 2>&1 | sed -n 's/.*java.specification.version = //p')" > "$SYS/release"
    java -jar "$ECJ" --system "$SYS" -proc:none -source 8 -target 8 -nowarn -encoding UTF-8 -d build/classes src/com/toolscreen/linux/Installer.java
fi
cp "$ROOT/native/build/libtoolscreen.so" resources/toolscreen-wrapper.sh resources/README.txt build/classes/native/
cp "$ROOT/LICENSE" build/classes/META-INF/LICENSE
printf 'Manifest-Version: 1.0\nMain-Class: com.toolscreen.linux.Installer\nImplementation-Title: Toolscreen for Linux\nImplementation-Version: %s-linux.1\n' "$VERSION" > build/MANIFEST.MF
mkdir -p "$ROOT/dist"
python3 -I - "$OUT" build/MANIFEST.MF build/classes <<'PY'
import os, sys, zipfile, stat
out, manifest, classes = sys.argv[1:4]
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    z.write(manifest, 'META-INF/MANIFEST.MF')
    for base, dirs, files in os.walk(classes):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(base, f)
            info = zipfile.ZipInfo(os.path.relpath(p, classes))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | (0o755 if f.endswith('.sh') else 0o644)) << 16
            with open(p, 'rb') as fh:
                z.writestr(info, fh.read())
PY
chmod +x "$OUT"
echo "built $OUT"
