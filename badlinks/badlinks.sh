#!/usr/bin/env bash
let week=7*24*60*60
for symlink in $(find $1 -type l)
do
    if [ ! -e "$symlink" ] && [ $(($(date +%s) - $(stat "$symlink" -c %Y))) -gt $week ];
    then echo "$symlink";
    fi
done
