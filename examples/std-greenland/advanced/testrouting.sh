#!/bin/bash

N=6

PREFIX=g10km

climate="-atmosphere searise_greenland -surface pdd -calving ocean_kill -ocean_kill_file pism_Greenland_5km_v1.1.nc"

hydro="-hydrology routing -report_mass_accounting -hydrology_tillwat_transfer_proportion 200.0"

YY=50
DT=0.25
etimes="0:$DT:$YY"


# work with ..._steady.nc file and use SIA
PRESTART=${PREFIX}_steady
STARTFILE=${PRESTART}_bwat.nc
oname=${PREFIX}_routing.nc
evarlist="thk,mask,csurf,bmelt,hydroinput,bwat,bwp,bwatvel,wallmelt,tillwat,temppabase,tempicethk_basal"
diagnostics="-extra_file extras_$oname -extra_times $etimes -extra_vars $evarlist"
ncap2 -O -s "bwat=0.0*tillwat" ${PRESTART}.nc $STARTFILE
ncatted -O -a long_name,bwat,m,c,"effective thickness of transportable subglacial water" $STARTFILE
ncatted -O -a standard_name,bwat,d,, $STARTFILE

echo
echo "TESTING -hydrology routing ON ${PRESTART}.nc:"
echo

cmd="mpiexec -n $N pismr -config_override searise_config.nc -i $STARTFILE $climate -ys 0 -y $YY $hydro $diagnostics -o $oname"
#$cmd

#exit

# work with ..._0.nc file and use SIA+SSA
PRESTART=${PREFIX}_0
STARTFILE=${PRESTART}_bwat.nc
oname=${PREFIX}_routing.nc
evarlist="thk,mask,cbase,csurf,tauc,bmelt,hydroinput,bwat,bwp,bwatvel,wallmelt,tillwat,temppabase,tempicethk_basal"
diagnostics="-extra_file extras_$oname -extra_times $etimes -extra_vars $evarlist"
ncap2 -O -s "bwat=0.0*tillwat" ${PRESTART}.nc $STARTFILE
ncatted -O -a long_name,bwat,m,c,"effective thickness of transportable subglacial water" $STARTFILE
ncatted -O -a standard_name,bwat,d,, $STARTFILE

echo
echo "TESTING -hydrology routing ON ${PRESTART}.nc:"
echo

cmd="mpiexec -n 2 pismr -config_override searise_config.nc -i $STARTFILE -stress_balance ssa+sia -topg_to_phi 15.0,40.0,-300.0,700.0 $climate -ys 0 -y $YY $hydro $diagnostics -o $oname"
$cmd

