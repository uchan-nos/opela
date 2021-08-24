#!/bin/sh

if [ $# -lt 1 ]
then
  echo "Usage: $0 parse_anime_dir"
  exit 1
fi

parse_anime_dir=$1
echo $parse_anime_dir

page=0
wmax=0
hmax=0
while :
do
  page_dir=$parse_anime_dir/$page
  if [ ! -d $page_dir ]
  then
    break
  fi

  convert -font Noto-Mono -pointsize 20 label:"$(cat $page_dir/stack.txt)" $page_dir/stack.gif
  dot -Tgif $page_dir/ast.dot > $page_dir/ast.gif
  convert +append $page_dir/stack.gif $page_dir/ast.gif $parse_anime_dir/page_$page.gif

  w=$(identify -format "%w" $parse_anime_dir/page_$page.gif)
  h=$(identify -format "%h" $parse_anime_dir/page_$page.gif)
  if [ $w -gt $wmax ]; then wmax=$w; fi
  if [ $h -gt $hmax ]; then hmax=$h; fi

  page=$(($page + 1))
done

last_page=$(($page - 1))

for page in $(seq 0 $last_page)
do
  convert $parse_anime_dir/page_$page.gif -background white -gravity northwest -extent "${wmax}x${hmax}" $parse_anime_dir/extended_page_$page.gif
done

#convert -delay 33 -loop 0 -repage $(identify -format "%wx%h" $parse_anime_dir/page_$last_page.gif)+0+0 $(ls -v $parse_anime_dir/page_*.gif) $parse_anime_dir/anime.gif
convert -delay 33 -loop 0 $(ls -v $parse_anime_dir/extended_page_*.gif) $parse_anime_dir/anime.gif
