#!/bin/sh
# Writes to a file, overwriting the existing contents if the file already exists

# Fail if the number of arguments isn't two
if [ $# -ne 2 ]
then
    echo "Expected two arguments, received $#"
    exit 1
fi

# Create path to file if it doesn't already exist
mkdir -p "${1%/*}"

# Creates the file if it doesn't exits, and overwrites contents with the provided value
echo $2 > $1
