// Copyright (C) 2011, 2012, 2013, 2014 PISM Authors
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#ifndef _POCONSTANTPIK_H_
#define _POCONSTANTPIK_H_

#include "PISMOcean.hh"
#include "NCVariable.hh"

//! A class defining the interface of a PISM ocean model modifier.

//! \brief A class implementing an ocean model.
//! Parameterization of sub-shelf melting with respect to sub-shelf heat flux like in Beckmann_Goosse 2003
class POConstantPIK : public PISMOceanModel {
public:
  POConstantPIK(IceGrid &g, const PISMConfig &conf);
  virtual ~POConstantPIK();

  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode update(double my_t, double my_dt);
  virtual PetscErrorCode sea_level_elevation(double &result);
  virtual PetscErrorCode shelf_base_temperature(IceModelVec2S &result);
  virtual PetscErrorCode shelf_base_mass_flux(IceModelVec2S &result);

  virtual void add_vars_to_output(std::string keyword, std::set<std::string> &result);
  virtual PetscErrorCode define_variables(std::set<std::string> vars, const PIO &nc,
                                          PISM_IO_Type nctype);
  virtual PetscErrorCode write_variables(std::set<std::string> vars, const PIO &nc);
protected:
  IceModelVec2S *ice_thickness; // is not owned by this class
  NCSpatialVariable shelfbmassflux, shelfbtemp;
private:
  PetscErrorCode allocate_POConstantPIK();
  double meltfactor;
};

#endif /* _POCONSTANTPIK_H_ */
