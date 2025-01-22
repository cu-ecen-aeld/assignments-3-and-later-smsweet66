#!/bin/sh
# Searches for files within the specified folder that contain the specified string

# Fail if the number of arguments isn't two
if [ $# -ne 2 ]
then
    echo "Expected two arguments, received $#"
    exit 1
fi

# fail if folder doesn't exist
if [ ! -d $1 ]
then
    echo "Directory $1 does not exist"
    exit 1
fi

# creates a list of all files under the given folder
files=$(find $1 -type f)

# gets the number of files under the given folder
num_files=$(find $1 -type f | wc -l)

# gets the total number of matching lines from each of the files
num_lines=0
for file in $files
do
    num_lines=$(($num_lines + $(cat $file | grep $2 -c)))
done

echo "The number of files are $num_files and the number of matching lines are $num_lines"
