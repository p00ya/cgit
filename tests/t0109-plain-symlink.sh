#!/bin/sh

. ./setup.sh

prepare_tests "Ensure symbolic links work in plain view"

mkrepo2() {
	name=$1
	dir=$PWD
	test -d $name && return
	printf "Creating testrepo %s\n" $name
	mkdir -p $name
	cd $name
	git init
	mkdir sub1
	mkdir sub1/sub2
	echo file A > file-a
	echo file B > sub1/file-b
	echo file C > sub1/sub2/file-c
	touch this_is_the_root
	touch sub1/this_is_sub1
	touch sub1/sub2/this_is_sub2
	ln -s file-a link-a
	ln -s link-a double-a
	ln -s sub1 link-sub1
	ln -s sub1/file-b link-b
	ln -s sub1/sub2 link-sub2
	ln -s self self
	ln -s . dot
	ln -s .. up
	ln -s sub1/../.. up2
	ln -s ../foo up-file
	ln -s /foo/bar absolute
	ln -s loop-b loop-a
	ln -s loop-a loop-b
	ln -s foo broken
	ln -s broken double-broken
	ln -s foo/bar broken-dir
	ln -s .. sub1/up
	ln -s .. sub1/sub2/up
	ln -s ../file-a sub1/link-a
	ln -s ./file-b sub1/link-b
	ln -s ../sub1/file-b sub1/link-b2
	ln -s ../sub1/sub2/file-c sub1/link-c
	git add .
	git commit -m 'initial commit'
	cd $dir
}

add_repo() {
	dir=$1
	name=$2
	cat >>trash/cgitrc <<EOF

repo.url=$name
repo.path=$PWD/$dir/.git
repo.desc=the $name repo
EOF
}

mkrepo2 trash/repos/linker
add_repo trash/repos/linker linker

exist() {
	message="$1"
	path="$2"
	value="$3"
	run_test "$message" \
		"cgit_url \"linker/plain/$path\" | grep -e \"$value\""
}

nonexist() {
	message="$1"
	path="$2"
	run_test "$message" \
		"cgit_url \"linker/plain/$path\" | grep -e '^Status: 4'"
}


exist 'regular file' 'file-a' 'file A'
exist 'link to regular file' 'link-a' 'file A'
exist 'double link to regular file' 'double-a' 'file A'
exist 'regular file in subdirectory' 'sub1/file-b' 'file B'
exist 'link to regular file in subdirectory' 'link-b' 'file B'

exist 'root directory' '' 'this_is_the_root'
exist 'link to root directory' 'dot' 'this_is_the_root'
exist 'subdirectory' 'sub1' 'this_is_sub1'
exist 'link to subdirectory' 'link-sub1' 'this_is_sub1'
exist 'link to nested subdirectory' 'link-sub2' 'this_is_sub2'
exist 'file in link to subdirectory' 'link-sub2/file-c' 'file C'

exist 'upward link to file 1' 'sub1/link-a' 'file A'
exist 'upward link to file 2' 'sub1/link-b' 'file B'
exist 'upward link to file 3' 'sub1/link-b2' 'file B'
exist 'upward link to file 4' 'sub1/link-c' 'file C'
exist 'upward link to parent directory' 'sub1/sub2/up' 'this_is_sub1'
exist 'upward link to root directory' 'sub1/up' 'this_is_the_root'
exist 'file in link to parent directory' 'sub1/up/file-a' 'file A'
exist 'directory in link to parent directory' 'sub1/up/sub1' 'this_is_sub1'
exist 'does not collapse ".." naively' 'link-sub2/..' 'this_is_sub1'

exist 'recursive link to root directory' 'dot/dot/dot' 'this_is_the_root'
exist 'file in recursive link to root directory' 'dot/dot/dot/file-a' 'file A'
exist 'valid loop' 'sub1/up/sub1/up/sub1/up/file-a' 'file A'

nonexist 'invalid link: self-referential' 'self'
nonexist 'invalid link: endless loop' 'loop-a'
nonexist 'invalid link: absolute path' 'absolute'
nonexist 'invalid link: broken symlink' 'broken'
nonexist 'invalid link: valid link to broken symlink' 'double-broken'
nonexist 'invalid link: broken symlink including slash' 'broken-dir'
nonexist 'invalid link: broken symlink as directory' 'broken/xyz'
nonexist 'invalid link: up too high' 'up'
nonexist 'invalid link: up too high 2' 'up2'
nonexist 'invalid link: file in up too high' 'up/foo'
nonexist 'invalid link: link to file in up too high' 'up-file'

tests_done
