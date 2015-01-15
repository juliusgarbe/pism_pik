#!/bin/bash

PISM_PATH=$1
MPIEXEC=$2

# Test name:
echo "Test #20: GPBLD flow law regression."
# The list of files to delete when done.
files="flowtable-20.txt diff-20.txt"

rm -f $files

$PISM_PATH/flowlaw_test -flow_law gpbld > flowtable-20.txt
# compare results
diff flowtable-20.txt - > diff-20.txt <<END-OF-OUTPUT
flow law:   "Glen-Paterson-Budd-Lliboutry-Duval"
pressure = 1.785e+07 Pa = (hydrostatic at depth 2000.00 m)
flowtable:
  (dev stress)   (abs temp) (liq frac) =   (flow)
      1.00e+04      241.740      0.000 = 4.657918e-18
      1.00e+04      266.740      0.000 = 1.451147e-16
      1.00e+04      271.740      0.000 = 4.542999e-16
      1.00e+04      271.740      0.005 = 8.660092e-16
      5.00e+04      241.740      0.000 = 1.164479e-16
      5.00e+04      266.740      0.000 = 3.627868e-15
      5.00e+04      271.740      0.000 = 1.135750e-14
      5.00e+04      271.740      0.005 = 2.165023e-14
      1.00e+05      241.740      0.000 = 4.657918e-16
      1.00e+05      266.740      0.000 = 1.451147e-14
      1.00e+05      271.740      0.000 = 4.542999e-14
      1.00e+05      271.740      0.005 = 8.660092e-14
      1.50e+05      241.740      0.000 = 1.048031e-15
      1.50e+05      266.740      0.000 = 3.265081e-14
      1.50e+05      271.740      0.000 = 1.022175e-13
      1.50e+05      271.740      0.005 = 1.948521e-13
END-OF-OUTPUT

if [ $? != 0 ];
then
    exit 1
fi

rm -f $files; exit 0

