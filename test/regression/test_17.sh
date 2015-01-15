#!/bin/bash

PISM_PATH=$1
MPIEXEC=$2

# Test name:
echo "Test #17: verif test G regression: thermo SIA w. time-dependent SMB."
# The list of files to delete when done.
files="test_17-G-out.txt"

rm -f $files

# run test G
OPTS="-test G -Mbz 1 -Mz 31 -y 1000 -o_size none -verbose 1"
$PISM_PATH/pismv -Mx 31 -My 31 $OPTS   > test_17-G-out.txt
$PISM_PATH/pismv -Mx 41 -My 41 $OPTS  >> test_17-G-out.txt

# compare results
diff test_17-G-out.txt -  <<END-OF-OUTPUT
NUMERICAL ERRORS evaluated at final time (relative to exact solution):
geometry  :    prcntVOL        maxH         avH   relmaxETA
               0.780389   32.443567    7.149022    0.016300
temp      :        maxT         avT    basemaxT     baseavT
               0.835466    0.249417    0.747461    0.151139
Sigma     :      maxSig       avSig
               7.208338    0.955225
surf vels :     maxUvec      avUvec        maxW         avW
               0.945140    0.199932    0.028361    0.004027
NUM ERRORS DONE
NUMERICAL ERRORS evaluated at final time (relative to exact solution):
geometry  :    prcntVOL        maxH         avH   relmaxETA
               0.782888   30.772167    7.242054    0.015477
temp      :        maxT         avT    basemaxT     baseavT
               0.894900    0.248302    0.746664    0.155312
Sigma     :      maxSig       avSig
               7.185588    0.913964
surf vels :     maxUvec      avUvec        maxW         avW
               0.886013    0.194881    0.027400    0.004219
NUM ERRORS DONE
END-OF-OUTPUT

if [ $? != 0 ];
then
    exit 1
fi

rm -f $files; exit 0

