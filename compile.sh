#/bin/sh

dir=$(dirname $1)
name=$(basename $1)
name=$(echo $name | cut -d '.' -f 1)
./main $1 && ./qbe $1.ssa > $dir/$name.s && cc -o $dir/$name $dir/$name.s
