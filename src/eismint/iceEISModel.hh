// Copyright (C) 2004-2015 Jed Brown, Ed Bueler and Constantine Khroulev
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

#ifndef __iceEISModel_hh
#define __iceEISModel_hh

#include "base/iceModel.hh"

namespace pism {


//! Derived class for doing EISMINT II simplified geometry experiments.  
/*!
  These experiments use the thermomechanically-coupled, non-polythermal shallow
  ice approximation.  Experiment H does \e not \e recommended SIA-sliding
  paradigm.  See \ref EISMINT00 and Appendix B of \ref BBssasliding.
*/
class IceEISModel : public IceModel {
public:
  IceEISModel(IceGrid::Ptr g, Context::Ptr ctx, char experiment);
  virtual void set_vars_from_options();
  virtual void allocate_stressbalance();
  virtual void allocate_couplers();
protected:
  char m_experiment;

  virtual void generateTroughTopography(IceModelVec2S &result);  // for experiments I,J
  virtual void generateMoundTopography(IceModelVec2S &result);   // for experiments K,L
};

} // end of namespace pism

#endif /* __iceEISModel_hh */

