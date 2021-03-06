/* Copyright (C) 2015 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "PISMBedDef.hh"
#include "base/util/IceGrid.hh"
#include "base/util/pism_const.hh"
#include "base/util/MaxTimestep.hh"

namespace pism {
namespace bed {

PBNull::PBNull(IceGrid::ConstPtr g)
  : BedDef(g) {
  // empty
}

void PBNull::init_impl() {
  BedDef::init_impl();

  m_log->message(2,
             "* Initializing the dummy (no-op) bed deformation model...\n");
  m_uplift.set(0.0);
}

MaxTimestep PBNull::max_timestep_impl(double t) {
  (void) t;
  return MaxTimestep();
}

void PBNull::update_with_thickness_impl(const IceModelVec2S &ice_thickness,
                                        double t, double dt) {
  (void) ice_thickness;
  (void) t;
  (void) dt;
  // This model does not update bed topography or bed uplift.
}

} // end of namespace bed
} // end of namespace pism
