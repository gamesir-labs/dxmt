#!/bin/sh
set -eu

usage() {
  printf '%s\n' \
    'usage: scripts/check-commit-message.sh --file PATH' \
    '       scripts/check-commit-message.sh --commit COMMIT' \
    '       scripts/check-commit-message.sh --subject SUBJECT' >&2
  exit 2
}

if [ "$#" -ne 2 ]; then
  usage
fi

case $1 in
  --file)
    if [ ! -f "$2" ]; then
      printf 'commit message file does not exist: %s\n' "$2" >&2
      exit 2
    fi
    subject=$(sed -n '1p' "$2")
    label=$2
    ;;
  --commit)
    if ! subject=$(git show -s --format=%s "$2" 2>/dev/null); then
      printf 'commit does not exist: %s\n' "$2" >&2
      exit 2
    fi
    label=$(git rev-parse --short "$2")
    ;;
  --subject)
    subject=$2
    label='provided subject'
    ;;
  *)
    usage
    ;;
esac

pattern='^[a-z][a-z0-9-]*\([a-z0-9][a-z0-9._/-]*\)!?: [^[:space:]].*$'

if printf '%s\n' "$subject" | LC_ALL=C grep -Eq "$pattern"; then
  exit 0
fi

printf 'invalid commit message (%s): %s\n' "$label" "$subject" >&2
printf '%s\n' \
  'expected: type(scope): subject' \
  'example:  fix(builder): validate commit messages before push' >&2
exit 1
