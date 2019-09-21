#!/usr/bin/env bash
set -e
CWD="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CONTINUE=$(find tests -name "*single_main.cpp" | wc -l)
(( $CONTINUE > 0 )) && echo "mkn.sh is not runnning" && exit 0

for f in $(grep -lR "int main(" $CWD/tests); do
  cd $(dirname ${f})
  [[ -f "$PWD/single_main.cpp" ]] && continue;
  for file in $(find . -maxdepth 1 -name "*.cpp" -type f ); do
    file="${file:2}"
    [[ "$PWD/$file" == "${f}" ]] && continue;
    cat $PWD/$file >> $PWD/single_main.cpp
  done
  cat ${f} >> $PWD/single_main.cpp
done
cd $CWD
for f in $(find $CWD/tests -name "input_config.h.in"); do
  SEDCWD=$(echo "$(dirname $f)" | sed 's/\//\\\//g')
  sed "s/\"\@CMAKE_CURRENT_SOURCE_DIR\@\/\"/\"${SEDCWD}\/\"/g" $f > $(dirname $f)/input_config.h
done

python3 $CWD/tests/core/numerics/pusher/test_pusher.py $CWD 2>&1 > /dev/null
python3 $CWD/tests/core/numerics/interpolator/interpolator_test.py $CWD 2>&1 > /dev/null
python3 $CWD/tests/core/numerics/ampere/test_ampere.py
python3 $CWD/tests/core/numerics/faraday/test_faraday.py
python3 $CWD/tests/core/numerics/ohm/test_ohm.py
for py in $(find $CWD/tests/core/data/gridlayout -name "*.py"); do python3 $py; done
python3 $CWD/tests/samrai_interface/data/field/coarsening/test_coarsen_field.py
find $CWD/tests | grep "point\/single_main" | xargs rm # test not read
cp $CWD/src/phare/setpythonpath.py.in $CWD/setpythonpath.py
