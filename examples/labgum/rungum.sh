#!/bin/bash

if [ $# -lt 2 ] ; then
  echo "rungum.sh ERROR: needs two arguments ... ENDING NOW"
  echo "  format:"
  echo "    rungum.sh PROCS MX"
  echo "  where"
  echo "    PROCS     = 1,2,3,... is number of MPI processes"
  echo "    MX        = number of grid points in x,y directions;  MX -> cell width:"
  echo "                53 -> 10mm,  105 -> 5mm, 209 -> 2.5mm, 521 -> 1mm"
  exit
fi

NN="$1"
myMx="$2"

pismexec="pismr"

initfile=initlab$myMx.nc
oname=lab$myMx.nc

grid="-Mx $myMx -My $myMx -Mz 26 -Lz 0.025 -Mbz 0 -Lbz 0 -z_spacing equal"

physics="-config_override gumparams.nc -energy none -sia_flow_law isothermal_glen -sia_e 1.0 -gradient mahaffy"

endtime=2.3640e-05   # = 746 / 31556926; = 746 s in years; Sayag personal communication

ts_dt=3.1689e-09     # = 0.1 / 31556926 = 0.1 s
timediag="-ts_file ts_$oname -ts_times $ts_dt:$ts_dt:$endtime"

ex_dt=3.1689e-07     # = 10 / 31556926 = 10 s
exvars="diffusivity,cflx,cbar,csurf,mask,thk,wvelsurf"
exdiag="-extra_file ex_$oname -extra_vars $exvars -extra_times 0:$ex_dt:$endtime"

mpiexec -n $NN $pismexec -boot_file $initfile $grid $physics \
    $timediag $exdiag -ys 0.0 -y $endtime -max_dt $ts_dt -o $oname

