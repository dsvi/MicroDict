#!/bin/sh
if [ -z "$BIN" ]; then
  for candidate in "$(dirname "$0")/../../build/microdict" \
                   "$(dirname "$0")/../../build-out/microdict"; do
    if [ -x "$candidate" ]; then
      BIN=$candidate
      break
    fi
  done
fi

if [ ! -x "$BIN" ]; then
  echo "migtest.sh: no binary to test (set BIN=); looked next to $(dirname "$0")"
  exit 1
fi
fail=0

own_xml() {
  cat <<EOF
<config>
  <paths>
    <path recursive="1">$1</path>
  </paths>
  <soundDirs/>
  <hunspell/>
  <preferences>
    <darkMode>1</darkMode>
    <darkReaderMode>1</darkReaderMode>
    <escKeyHidesMainWindow>1</escKeyHidesMainWindow>
    <zoomFactor>1.5</zoomFactor>
  </preferences>
  <groups>
    <group name="OwnGroup">
      <dictionary id="1234567890"/>
    </group>
  </groups>
  <mutedDictionaries/>
  <help/>
  <version/>
</config>
EOF
}

check() {
  if [ "$2" = "$3" ]; then
    echo "  ok   : $1"
  else
    echo "  FAIL : $1 (expected [$2], got [$3])"
    fail=1
  fi
}

only() {
  ls "$1" 2>/dev/null | grep -v '^pulse$' | tr '\n' ' ' | sed 's/ $//'
}

run_app() {
  timeout 10 "$BIN" >/dev/null 2>&1
  pkill -x microdict 2>/dev/null || true
  sleep 1
}

echo "=== A. redirected XDG dirs ==="
T=/tmp/migA
rm -rf $T
mkdir -p $T/config $T/data $T/cache $T/tmp
export XDG_CONFIG_HOME=$T/config XDG_DATA_HOME=$T/data XDG_CACHE_HOME=$T/cache
export TMPDIR=$T/tmp
export QT_QPA_PLATFORM=offscreen
run_app

check "own config created" "yes" "$([ -f $T/config/microdict/config ] && echo yes || echo no)"
check "data dir is microdict" "yes" "$([ -d $T/data/microdict ] && echo yes || echo no)"
check "cache dir is microdict" "yes" "$([ -d $T/cache/microdict ] && echo yes || echo no)"
check "config, data and cache dirs hold only microdict" "microdict microdict microdict" \
  "$(only $T/config) $(only $T/data) $(only $T/cache)"

own_xml /srv/own-dicts > $T/config/microdict/config
run_app
check "own config still holds what was written to it" "1" \
  "$(grep -c 'srv/own-dicts' $T/config/microdict/config)"
check "own config is still the only config file" "1" \
  "$(find $T/config -mindepth 2 -type f -name config | wc -l)"
check "config dir still holds only microdict" "microdict" "$(only $T/config)"

echo "=== B. default HOME, no XDG variables ==="
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME
export TMPDIR=/tmp/migB/tmp
rm -rf /tmp/migB
mkdir -p "$TMPDIR"
HOME=$(eval echo "~")
rm -rf $HOME/.config/microdict $HOME/.local/share/microdict $HOME/.cache/microdict
snapshot() {
  find "$HOME/.config" "$HOME/.local/share" "$HOME/.cache" -mindepth 1 \
       ! -path "$HOME/.config/microdict*" \
       ! -path "$HOME/.local/share/microdict*" \
       ! -path "$HOME/.cache/microdict*" \
       ! -path "$HOME/.config/pulse*" \
       -printf '%p %s\n' 2>/dev/null | sort
}
before=$(snapshot)

run_app

check "own config created under ~/.config/microdict" "yes" \
  "$([ -f $HOME/.config/microdict/config ] && echo yes || echo no)"
check "~/.local/share/microdict used" "yes" \
  "$([ -d $HOME/.local/share/microdict ] && echo yes || echo no)"
check "nothing outside the microdict dirs touched" "$before" "$(snapshot)"

echo "=== C. a broken config of its own is not silently rewritten ==="
T=/tmp/migC
rm -rf $T
mkdir -p $T/config/microdict $T/data $T/cache $T/tmp
export TMPDIR=$T/tmp
export XDG_CONFIG_HOME=$T/config XDG_DATA_HOME=$T/data XDG_CACHE_HOME=$T/cache
printf '<config>\n  <paths>\n  <path>/srv/truncated</path>\n' > $T/config/microdict/config
sha3=$(sha256sum $T/config/microdict/config | cut -d' ' -f1)

run_app

check "broken config left byte-identical" "$sha3" \
  "$(sha256sum $T/config/microdict/config | cut -d' ' -f1)"
check "config dir holds only microdict" "microdict" "$(only $T/config)"

echo "=== migtest.sh: $([ $fail -eq 0 ] && echo 'config-directory checks passed' || echo 'FAILED') ==="
exit $fail
