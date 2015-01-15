// Copyright (C) 2012, 2013, 2014 PISM Authors
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

#ifndef _PA_PALEO_PRECIP_H_
#define _PA_PALEO_PRECIP_H_

#include "PScalarForcing.hh"
#include "PAModifier.hh"

class PA_paleo_precip : public PScalarForcing<PISMAtmosphereModel,PAModifier>
{
public:
  PA_paleo_precip(IceGrid &g, const PISMConfig &conf, PISMAtmosphereModel* in);
  virtual ~PA_paleo_precip();

  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode init_timeseries(double *ts, unsigned int N);

  virtual PetscErrorCode mean_precipitation(IceModelVec2S &result);

  virtual PetscErrorCode precip_time_series(int i, int j, double *values);

  virtual void add_vars_to_output(std::string keyword, std::set<std::string> &result);

  virtual PetscErrorCode define_variables(std::set<std::string> vars, const PIO &nc,
                                          PISM_IO_Type nctype);

  virtual PetscErrorCode write_variables(std::set<std::string> vars, const PIO &nc);

protected:
  NCSpatialVariable air_temp, precipitation;
  double m_precipexpfactor;
  std::vector<double> m_scaling_values;
private:
  PetscErrorCode allocate_PA_paleo_precip();
};

#endif /* _PA_PALEO_PRECIP_H_ */
