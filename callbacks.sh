#!/bin/bash
for var in "$@"; do
    case "$var" in
        -l|--list)
            echo "c++"
            exit 0
            ;;
        -h|--help)
            echo "Usage: callbacks.sh --list"
            exit 0
            ;;
        *)
            echo "Unknown option $var" >&2
            exit 1
            ;;
    esac
done
exit 0
