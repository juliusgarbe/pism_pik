// Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016 PISM Authors
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

#ifndef _PISMMOHRCOULOMBYIELDSTRESS_H_
#define _PISMMOHRCOULOMBYIELDSTRESS_H_

#include "PISMYieldStress.hh"
#include "base/util/iceModelVec.hh"

namespace pism {

namespace hydrology {
class Hydrology;
}

//! @brief PISM's default basal yield stress model which applies the
//! Mohr-Coulomb model of deformable, pressurized till.
class MohrCoulombYieldStress : public YieldStress {
public:
  MohrCoulombYieldStress(IceGrid::ConstPtr g, hydrology::Hydrology *hydro);
  virtual ~MohrCoulombYieldStress();

  void set_till_friction_angle(const IceModelVec2S &input);
protected:
  virtual void init_impl();
  virtual MaxTimestep max_timestep_impl(double t);
  virtual void update_impl();
  virtual void write_variables_impl(const std::set<std::string> &vars, const PIO &nc);
  virtual void add_vars_to_output_impl(const std::string &keyword, std::set<std::string> &result);
  virtual void define_variables_impl(const std::set<std::string> &vars, const PIO &nc,
                                     IO_Type nctype);

  void topg_to_phi();
  void tauc_to_phi();
protected:
  bool m_topg_to_phi, m_tauc_to_phi, m_iterative_phi;
  IceModelVec2S m_till_phi, m_tillwat, m_Po;
  IceModelVec2S m_target_usurf, m_diff_usurf,m_usurf,m_diff_mask;
  IceModelVec2S m_bwat;  // only allocated and used if basal_yield_stress.add_transportable_water = true
  hydrology::Hydrology *m_hydrology;
  double m_last_time, m_last_inverse_time;
};

} // end of namespace pism

#endif /* _PISMMOHRCOULOMBYIELDSTRESS_H_ */
