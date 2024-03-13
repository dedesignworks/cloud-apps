#!/bin/bash

# Add directories here. One directory per line.
clang_dirs=(
    App
)

clang_files=(
)

# Add directories to be excluded here. One directory per line.
clang_exclude_dirs=(
)

# Add files to be excluded here. One directory per line.
clang_exclude_files=(
)

findargs=()
for i in "${clang_exclude_dirs[@]}"
do
    findargs+=('!' '-path' "$i/*")
done

for i in "${clang_exclude_files[@]}"
do
    findargs+=('!' '-path' "$i")
done

for i in "${clang_dirs[@]}"
do
    find $i -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' "${findargs[@]}" -exec clang-format --verbose -style=file -i {} \;
done

for i in "${clang_files[@]}"
do
    find $i -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format --verbose -style=file -i {} \;
done