// Copyright (C) 2011, 2012, 2013, 2014 Andy Aschwanden and Constantine Khroulev
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

#ifndef _PSELEVATION_H_
#define _PSELEVATION_H_

#include "PISMSurface.hh"
#include "iceModelVec2T.hh"
#include "PISMAtmosphere.hh"

//! \brief A class implementing a elevation-dependent temperature and mass balance model.
class PSElevation : public PISMSurfaceModel {
public:
  PSElevation(IceGrid &g, const PISMConfig &conf);

  virtual PetscErrorCode init(PISMVars &vars);
  virtual void attach_atmosphere_model(PISMAtmosphereModel *input);

  virtual void get_diagnostics(std::map<std::string, PISMDiagnostic*> &dict,
                               std::map<std::string, PISMTSDiagnostic*> &ts_dict);

  virtual PetscErrorCode update(double my_t, double my_dt);
  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S &result);
  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result);
  virtual PetscErrorCode define_variables(std::set<std::string> vars, const PIO &nc, PISM_IO_Type nctype);
  virtual PetscErrorCode write_variables(std::set<std::string> vars, const PIO &nc);
  virtual void add_vars_to_output(std::string keyword, std::set<std::string> &result);
protected:
  NCSpatialVariable climatic_mass_balance, ice_surface_temp;
  IceModelVec2S *usurf;
  double T_min, T_max, z_T_min, z_T_max,
    m_min, m_max, m_limit_min, m_limit_max,
    z_m_min, z_ELA, z_m_max;
};

#endif /* _PSELEVATION_H_ */
