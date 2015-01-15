BOMBPROOF, PISM's numerical scheme for conservation of energy {#bombproofenth}
=========

[TOC]

@section intro Introduction

One of the essential goals for any thermomechanically-coupled numerical ice sheet model is a completely bombproof numerical scheme for the advection-conduction-reaction problem for the conservation of energy within the ice.  "Bombproof" means being as stable as possible in as many realistic modeling contexts as possible.  PISM's scheme is observed to be highly robust in practice, but it is also provably stable in a significant range of circumstances.  The scheme is special to shallow ice sheets because it makes specific tradeoff choices with respect to vertical velocity.  It is generic and low order in how it treats horizontal velocity.  In this page we state the scheme, prove its stability properties, and address the basal boundary condition.

The scheme is conditionally-stable.  The length of the time step is limited only by the maximum magnitude of the horizontal velocities within the ice, %i.e. horizontal CFL.  This condition for stability is included in the PISM adaptive time-stepping technique.

Accuracy is necessarily a second goal.  Our shallow scheme has truncation error @f$O(\Delta z^2)@f$ in many circumstances, though it reverts to lower order when it "detects trouble" in the form of large vertical velocities.  Overall the scheme has  order @f$O(\Delta t,\Delta x,\Delta y, \Delta z^2)@f$ in circumstances where the vertical ice flow velocity is small enough, relative to conductivity, and otherwise reverts to @f$O(\Delta t,\Delta x,\Delta y, \Delta z^1)@f$.

The conservation of energy problem for the ice is in terms of an enthalpy field [@ref AschwandenBuelerKhroulevBlatter].  The current scheme supercedes the cold-ice, temperature-based scheme described in the appendices of [@ref BBL] and in [@ref BBssasliding].  Compared to a cold-ice scheme, the enthalpy formulation does a better job of conserving energy, has a more-physical model for basal melt rate and drainage, and can model polythermal ice with any CTS topology [@ref AschwandenBuelerKhroulevBlatter].  The finite difference implementation of the enthalpy method is robust and avoids the CFL condition on vertical advection which was present in the older, cold-ice scheme.

The bedrock thermal problem is solved by splitting the timestep into an update of the bedrock temperature field, assuming the ice base is as constant temperature, and an update of the ice enthalpy field, by the BOMBPROOF scheme here, assuming the upward heat flux from the bedrock layer is constant during the timestep.  For more on the implementation, see the PISMBedThermalUnit class.

The region in which the conservation of energy equation needs to be solved changes over time.  This is an essential complicating factor in ice sheet modeling.  Also relevant is that the velocity field has a complicated provenance as it comes from different stress balance equations chosen at runtime.  These stress balances, especially with transitions in flow type, for instance at grounding lines, are incompletely understood when thermomechanically-coupled.  (The ShallowStressBalance instance owned by IceModel could be the SIA, the SSA, a hybrid of these, or other stress balances in future PISM versions.)  We will therefore not make, in proving stability, assumptions about the regularity of the velocity field in space or time other than boundedness.

Nor do we want the numerical scheme for advection to need any information about the velocity except its value at the beginning of the time step.  Thus the conservation of energy timestep is assumed to be split from the mass continuity time step and its associated stress balance solve.  We have not considered implementing a scheme which requires the Jacobian of the velocity field with respect to changes in enthalpy, for example.  At very least such a fully-implicit scheme would require blind iteration (%e.g. with no guarantee of convergence of the iteration).  The scheme we propose involves no such iteration.


@section iceequations Conservation of energy in a shallow ice sheet

In an enthalpy formulation [@ref AschwandenBuelerKhroulevBlatter, and references therein], the ice sheet is regarded as a mixture of two phases, solid and liquid, so that both cold and temperate ice with liquid ice matrix can be modeled.  The specific enthalpy field of the ice mixture is denoted @f$E(x,y,z,t)@f$ and has units @f$\text{J}\,\text{kg}^{-1}@f$.  (Within the PISM documentation the symbol @f$H@f$ is used for ice thickness so we use @f$E@f$ for enthalpy here and in the PISM source code versus "H" in [@ref AschwandenBuelerKhroulevBlatter].)  The conservation of energy equation is
@anchor basicEnergy

    @f[ \rho \frac{dE}{dt} = -\nabla \cdot \mathbf{q} + Q,\tag{basicEnergy} @f]

where @f$\rho@f$ is the mixture density.  The mixture density is assumed to be the same as ice density even if there is a nonzero liquid fraction, and the mixture is assumed to be incompressible [@ref AschwandenBuelerKhroulevBlatter].  The left and right sides of equation (@ref basicEnergy), and thus the quantity @f$Q@f$, have units @f$J\,\text{s}^{-1}\,\text{m}^{-3} = \text{W}\,\text{m}^{-3}@f$.

Neglecting the dependence of conductivity and heat capacity on temperature [@ref AschwandenBuelerKhroulevBlatter], the heat flux in cold ice and temperate ice is
@anchor heatflux

    @f[ \mathbf{q} = \begin{cases}
                      - k_i c_i^{-1} \nabla E, & \text{cold ice}, \\
                      - K_0 \nabla E, & \text{temperate ice}, \tag{heatflux} \\
                     \end{cases} @f]

where @f$k_i,c_i,K_0@f$ are constant [@ref AschwandenBuelerKhroulevBlatter].  The nonzero flux in the temperate ice case, may be conceptualized as a regularization of the "real" equation, or as a flux of latent heat carried by liquid water.  Also, @f$dE/dt@f$ stands for the material derivative of the enthalpy field, so the expanded form of (@ref basicEnergy) is

    @f[ \rho \left(\frac{\partial E}{\partial t} + \mathbf{U}\cdot \nabla E\right)
        = \nabla \cdot \left(\left\{\begin{matrix} k_i c_i^{-1} \\ K_0 \end{matrix}\right\} \nabla E \right) + Q, @f]

where @f$\mathbf{U}@f$ is the three dimensional velocity.  Thus advection is included.

The additive quantity @f$Q@f$ is the dissipation (strain-rate) heating,

    @f[ Q = \sum_{i,j=1}^3 D_{ij} \tau_{ij} @f]

where @f$D_{ij}@f$ is the strain rate tensor and @f$\tau_{ij}@f$ is the deviatoric stress tensor.  Reference [@ref BBssasliding] addresses how this term is computed in PISM, according to the shallow stress balance approximations; see method PISMStressBalance::get_volumetric_strain_heating().  (@f$Q@f$ is called @f$\Sigma@f$ in [@ref BBL, @ref BBssasliding] and in many places in the source code.)

Friction from sliding also is a source of heating.  It has units of W m-2 = J s-1 m-2, that is, the same units as the heat flux @f$\mathbf{q}@f$ above.  In formulas we write

    @f[ F_b = - \tau_b \cdot \mathbf{u}_b, @f]

where @f$\tau_b@f$ is the basal shear stress and @f$\mathbf{u}_b@f$ is the basal sliding velocity; the basal shear stress is oppositely-directed to the basal velocity.  For example, in the plastic case @f$\tau_b = - \tau_c \mathbf{u}_b / |\mathbf{u}_b|@f$ where @f$\tau_c@f$ is a positive scalar, the yield stress.  See method PISMStressBalance::get_basal_frictional_heating().  The friction heating is concentrated at @f$z=0@f$, and it enters into the basal boundary condition and melt rate calculation, addressed in section @ref melt below.

We use a shallow approximation of equation (@ref basicEnergy) which lacks horizontal
conduction terms  [@ref Fowler].  For the initial analysis of the core BOMBPROOF
scheme, we specialize to cold ice.  Within cold ice, the coefficient in the heat
flux is constant, so

    @f[ \nabla \cdot \mathbf{q} = - k_i c_i^{-1} \frac{\partial^2 E}{\partial z^2}. @f]

Therefore the equation we initially analyze is
@anchor basicShallow

    @f[
    \rho_i \left(\frac{\partial E}{\partial t} + \mathbf{U}\cdot \nabla E\right) = k_i c_i^{-1} \frac{\partial^2 E}{\partial z^2} + Q,\tag{basicShallow}
    @f]

We focus the analysis on the direction in which the enthalpy has largest derivative, 
namely with respect to the vertical coordinate @f$z@f$.  Rewriting equation
(@ref basicShallow) to emphasize the vertical terms we have
@anchor vertProblem

    @f[
    \rho_i \left(\frac{\partial E}{\partial t} + w \frac{\partial E}{\partial z}\right) 
         = k_i c_i^{-1}  \frac{\partial^2 E}{\partial z^2} + \Phi \tag{vertProblem}
    @f]
where

    @f[
    \Phi = Q - \rho_i \left(u \frac{\partial E}{\partial x}
                             + v \frac{\partial E}{\partial y}\right)
    @f]

We assume that the surface enthalpy
@f$E_s(t,x,y)@f$ (K) and the geothermal flux @f$G(t,x,y)@f$ (W m-2) at @f$z=0@f$
are given.  (The latter is the output of the PISMBedThermalUnit object, and it may
come from an evolving temperature field within the upper crust, the bedrock layer.
If a surface temperature is given then it will be converted to enthalpy by the
EnthalpyConverter class.)  Thus the boundary conditions to problem (@ref vertProblem)
are, therefore,
@anchor columnbcs

    @f[
    E(t,x,y,z=H)=E_s(t,x,y), \qquad -k_i c_i^{-1} \frac{\partial E}{\partial z}\Big|_{z=0} = G.\tag{columnbcs}
    @f]

For a temperate ice base, including any ice base below which there is liquid water,
the lower boundary condition is more interesting.  It is addressed below in section
@ref melt.

@section corebombproof The core BOMBPROOF scheme

For the discussion of the numerical scheme below, let @f$E_{ijk}^n@f$ be our
approximation to the exact enthalpy @f$E@f$ at the grid point with coordinates
@f$(x_i,y_j,z_k)@f$ at time @f$t_n@f$.  When @f$i,j@f$ are uninteresting we 
suppress them and write @f$E_k^n@f$, and we will use similar notation for numerical
approximations to the other quantities.  We put the horizontal advection terms in 
the source term @f$\Phi@f$ because we treat them explicitly, evaluating at time
@f$t_n@f$.  (Implicit or semi-implicit treatment of horizontal advection would
require a coupled system distributed across processors, a difficulty which is
currently avoided.)

The scheme we use for horizontal advection is explicit first-order upwinding.
There is a CFL condition for the scheme to be stable, in the absence of conduction,
based on the magnitude of the horizontal velocity components.  To state the upwind
scheme itself, let

    @f[\Up{f_{\bullet}}{\alpha} = \begin{cases}
                                     f_i-f_{i-1}, & \alpha \ge 0, \\
                                     f_{i+1}-f_i, & \alpha < 0.
                                  \end{cases}@f]
The approximate horizontal advection terms, and thus the approximation to the whole
term @f$\Phi@f$, are

    @f[\Phi_{ijk}^n = \Sigma_{ijk}^n - \rho_i 
	                   \left( u_{ijk}^n\,\frac{\Up{E_{\bullet jk}^n}{u_{ijk}^n}}{\Delta x}
	                          + v_{ijk}^n\,\frac{\Up{E_{i\bullet k}^n}{v_{ijk}^n}}{\Delta y} \right).@f]

The CFL stability condition for this part of the scheme is
@anchor CFL

    @f[
    \Delta t \,\left( \left|\frac{u_{ijk}^n}{\Delta x}\right|
                           + \left|\frac{v_{ijk}^n}{\Delta y}\right| \right) \le 1.\tag{CFL}
    @f]

The routine IceModel::computeMax3DVelocities() computes the 
maximum of velocity magnitudes.  This produces a time step restriction based on 
the above CFL condition.  Then IceModel::determineTimeStep() 
implements adaptive time-stepping based on this and other stability criteria.

In the analysis below we assume an equally-spaced grid @f$z_0,\dots,z_{M_z}@f$
with @f$\Delta z = z_{k+1} - z_k@f$.  In fact PISM has a remapping scheme in each 
column, wherein the enthalpy in a column of ice is stored on an unequally-spaced 
vertical grid, but is mapped to a fine, equally-spaced grid for the conservation
of energy computation described here.  (Similar structure applies to the age
computation.  See procedures IceModel::enthalpyAndDrainageStep() and
IceModel::ageStep().)

The @f$z@f$ derivative terms in (@ref vertProblem) will be approximated implicitly.  Let @f$\lambda@f$ be in the interval @f$0 \le \lambda \le 1@f$.  Suppressing indices @f$i,j@f$, the approximation to (@ref vertProblem) is
@anchor bombone

    @f{align*}
    \rho_i &\left( 
           \frac{E_k^{n+1} - E_k^n}{\Delta t}
           + \lambda w_k^n \frac{E_{k+1}^{n+1} - E_{k-1}^{n+1}}{2 \Delta z}
           + (1-\lambda) w_k^{n} \frac{\Up{E_{\bullet}^{n+1}}{w_k^{n}}}{\Delta z} \right) \\
         &= k_i c_i^{-1}\, \frac{E_{k+1}^{n+1} - 2 E_{k}^{n+1} + E_{k-1}^{n+1}}{\Delta z^2} + \Phi_k^n. \tag{bombone}
    @f}
    
Equation (@ref bombone), along with a determination of @f$\lambda@f$ by
(@ref lambdachoice) below, is the scheme BOMBPROOF.  It includes two approximations
of vertical advection,  implicit centered difference  (@f$\lambda = 1@f$) and
implicit first-order upwinding (@f$\lambda=0@f$).  They are combined using
nonnegative coefficients which sum to one, a convex combination.  The centered
formula has higher accuracy,

    @f[w_k^n \frac{E_{k+1}^{n+1} - E_{k-1}^{n+1}}{2 \Delta z}
	   = w \frac{\partial E}{\partial z} + O(\Delta t,\Delta z^2),@f]

while the first order upwind formula has lower accuracy,

	@f[w_k^{n} \frac{\Up{E_{\bullet}^{n+1}}{w_k^{n}}}{\Delta z}
	   = w \frac{\partial E}{\partial z} + O(\Delta t,\Delta z).@f]

Thus we prefer to use the centered formula when possible, but we apply (implicit)
upwinding when it is needed for its added stability benefits.

We now rewrite (@ref bombone) for computational purposes as one of a system of equations 
for the unknowns @f$\{E_k^{n+1}\}@f$.  In this system the coefficients will be
scaled so that the diagonal entries of the matrix have limit one as
@f$\Delta t\to 0@f$.  Let

	@f[ \nu = \frac{\Delta t}{\Delta z},
	 \qquad \text{and} \qquad R = \frac{k_i \Delta t}{\rho_i c_i \Delta z^2}.@f]

Now multiply equation (@ref bombone) by @f$\Delta t@f$, divide it by @f$\rho_i@f$,
and rearrange:
@anchor bombtwo

    @f{align*}
    &\left(-R - \nu w_k^n \uppair{1-\lambda/2}{\lambda/2}\right) E_{k-1}^{n+1}  
       + \left(1 + 2 R + \nu w_k^n (1-\lambda) \uppair{+1}{-1}\right) E_k^{n+1} \tag{bombtwo} \\
    &\qquad\qquad + \left(-R + \nu w_k^n \uppair{\lambda/2}{1-\lambda/2} \right) E_{k+1}^{n+1}
        = E_k^n + \Delta t \rho_i^{-1}\Phi_k^n 
    @f}
    
Here @f$\uppair{a}{b} = a@f$ when @f$w_k^n\ge 0@f$ and @f$\uppair{a}{b} = b@f$
when @f$w_k^n < 0@f$.

Equation (@ref bombtwo) has coefficients which are scaled to have no units.  It is 
ready to be put in the system managed by enthSystemCtx.

One way of stating the stability of first-order upwinding is to say it satisfies
a  "maximum principle" [@ref MortonMayers].  An example of a maximum principle
for this kind of finite difference scheme is that if @f$U_{k-1}^n,U_k^n,U_{k+1}^n@f$
are adjacent gridded values of some abstract quantity at time step @f$t_n@f$, and
if the next value satisfies the scheme
@anchor abstractexplicit

    @f{equation}
      U_k^{n+1} = C_{-1} U_{k-1}^n + C_0 U_k^n + C_{+1} U_{k+1}^n \tag{abstractexplicit}
    @f}
    
for *nonnegative* coefficients @f$C_i@f$ summing to one, @f$C_{-1} + C_0 + C_{+1} = 1@f$,  then it follows by the triangle inequality that

	@f[\min\{|U_{k-1}^n|, |U_k^n|, |U_{k+1}^n|\} 
	       \le |U_k^{n+1}| \le \max\{|U_{k-1}^n|, |U_k^n|, |U_{k+1}^n|\}.@f]

Thus a "wiggle" cannot appear in @f$\{U_k^{n+1}\}@f$ if previous values @f$\{U_k^n\}@f$ were smoother.  The proof below shows the corresponding "wiggle-free" property for scheme (@ref bombtwo).

However, the pure implicit centered difference scheme (@f$\lambda=1@f$), namely
@anchor centered

    @f{align*}
    &\left(-R - \nu w_k^n/2\right) E_{k-1}^{n+1} + \left(1 + 2 R\right) E_k^{n+1} \tag{centered} \\
    &\qquad\qquad + \left(-R + \nu w_k^n/2\right) E_{k+1}^{n+1} 
                  = E_k^n + \Delta t \rho_i^{-1}\Phi_k^n 
    @f}
    
is *less stable* than implicit first-order upwinding.  It is less stable 
in the same sense that Crank-Nicolson is a less stable scheme than 
backwards Euler for the simplest heat equation @f$u_t = u_{xx}@f$ [@ref MortonMayers].
In fact, although oscillatory modes cannot grow exponentially under equation (@ref centered),
those modes *can* appear when none are present already, even in the homogeneous case
@f$\Phi_k^n=0@f$.

@section theory Stability properties of the BOMBPROOF scheme

We want to be precise about the phrase "unconditionally stable" for BOMBPROOF.
To do so we consider somewhat simplified cases which are amenable to analysis, and
we prove two stability properties.  These stability properties identify the
precise advantages of BOMBPROOF.

@subsection stabilitythm Theorem (stating the stability properties).
Assume, for the precise but limited assertion of this theorem,
that the surface temperature @f$T_s@f$ and the geothermal flux @f$G@f$ are constant 
in time.  Assume also that the entire source function @f$\Phi@f$ is identically zero 
(but see comments below).  Fix an equally-spaced vertical grid @f$z_0=0 < z_1 < \dots < z_N=H@f$,
so that the upper grid point coincides with the surface of the ice.  With these assumptions, if
@anchor lambdachoice

    @f[
    \lambda = \min\left\{1, \quad 
    	               \min_{k=0,\dots,N}\left\{\frac{2 k_i}{|w_k^n| \rho_i c_i \Delta z}\right\}
    	               \quad \right\},\tag{lambdachoice}
    @f]
    
reset at each time step @f$n@f$, then scheme (@ref bombone, @ref bombtwo) is
unconditionally-stable in the following two senses:

1. A maximum principle applies without further assumptions.
 
2. Suppose we freeze the coefficients of the problem to have constant
   values in time and space. (Concretely, we assume that @f$\lambda@f$
   is chosen independently of the time step @f$n@f$, and that
   @f$\Delta t@f$ is the same for each time step. We assume constant
   vertical velocity @f$w_k^n=w_0@f$. We also consider a
   spatially-periodic or unbounded version of our problem, with no
   boundary conditions.) Then a von Neumann analysis of the constant
   coefficient problem yields a growth factor less than one for all
   modes on the grid.

@subsection remark Remarks.
The phrases *maximum principle* and *von Neumann analysis* will be precisely illustrated in the following proof.  Both approaches are in [@ref MortonMayers].  There is additional information on the von Neumann analysis of implicit finite difference methods for advection in [@ref Strikwerda].

These statements also apply in case @f$k_i=0@f$, in which case (@ref lambdachoice) implies @f$\lambda=0@f$, and the method reduces to implicit first-order upwinding.  (Implicit first-order upwinding has properties 1 and 2 [@ref Strikwerda].)  The case @f$k_i=0@f$ is relevant because it applies to the least-transport model of temperate ice in which there is zero enthalpy conduction.  (One reasonable model for temperate ice is to assume no transport of the liquid fraction, whether diffusive transport or otherwise, and to ignore conduction along the temperature gradient, because the gradient is only from pressure-melting temperature differences.)

@subsection proofone Proof of 1.

In the case considered for the maximum principle, with @f$\Phi_k^n=0@f$, 
we can rewrite (@ref bombtwo) as
@anchor formax

    @f{align*}
    &\left(1 + 2 R + \nu w_k^n (1-\lambda) \uppair{+1}{-1}\right) E_k^{n+1} \\
    &\qquad = E_k^n + \left(R + \nu w_k^n \uppair{1-\lambda/2}{\lambda/2}\right) E_{k-1}^{n+1}
                    + \left(R - \nu w_k^n \uppair{\lambda/2}{1-\lambda/2}\right) E_{k+1}^{n+1}.
                    \tag{formax}
    @f}
    
We claim that with choice (@ref lambdachoice) for @f$0 \le \lambda \le 1@f$, all 
coefficients in (@ref formax) are nonnegative.  At one extreme, in 
the upwinding case (@f$\lambda=0@f$), all the coefficients are nonnegative.  Otherwise, note that
@f$\nu w_k^n (1-\lambda) \uppair{+1}{-1}@f$ is nonnegative for any valid value 
of @f$\lambda@f$ and for any value of @f$w_k^n@f$, noting the meaning of the @f$\uppair{+1}{-1}@f$
symbol.  Thus the coefficient on the left is always nonnegative.  The coefficient of 
@f$E_{k-1}^{n+1}@f$ is clearly nonnegative for any valid value of @f$\lambda@f$ if @f$w_k^n \ge 0@f$.
The coefficient of @f$E_{k+1}^{n+1}@f$ is clearly nonnegative for any valid value of @f$\lambda@f$ if 
@f$w_k^n \le 0@f$.

Therefore the only concerns are for the coefficient of @f$E_{k-1}^{n+1}@f$ when @f$w_k^n\le 0@f$ and the 
coefficient of @f$E_{k+1}^{n+1}@f$ when @f$w_k^n\ge 0@f$.  But if @f$\lambda@f$ is smaller than 
@f$2k_i/(|w_k^n| \rho_i c_i \Delta z)@f$ then 

    @f{align*}
    R - \nu |w_k^n| (\lambda/2) &= \frac{k_i \Delta t}{\rho_i c_i \Delta z^2}
    	                                       - \frac{\Delta t |w_k^n|}{\Delta z} \frac{\lambda}{2}
    	        &\ge \frac{k_i \Delta t}{\rho_i c_i \Delta z^2}
    	            - \frac{\Delta t |w_k^n|}{\Delta z} \frac{k_i}{|w_k^n| \rho_i c_i \Delta z} = 0.
    @f}
    
Thus all the coefficients in (@ref formax) are nonnegative.  On the other hand, in equation (@ref formax), all coefficients on the right side sum to

    @f[ 1+2R+\nu w_k^n \uppair{1-\lambda}{-1+\lambda} = 1+2R+\nu w_k^n (1-\lambda) \uppair{+1}{-1}, @f]

which is exactly the coefficient on the left side of (@ref formax).  It follows that

    @f[ E_k^{n+1} = a_k E_k^n + b_k E_{k-1}^{n+1} + c_k E_{k+1}^{n+1} @f]

where @f$a_k,b_k,c_k@f$ are positive and @f$a_k+b_k+c_k=1@f$.  Thus a maximum principle applies [@ref MortonMayers]. **END OF PROOF OF 1.** 

@subsection prooftwo Proof of 2.
As a von Neumann analysis is much more restrictive than the analysis above, we will be brief.  
Let's assume the velocity is downward, @f$w_0<0@f$; the other case is similar.  Equation
(@ref bombtwo) becomes 
@anchor prevon

    @f{align*}
    &\left(-R - \nu w_0 (\lambda/2)\right) E_{k-1}^{n+1}  
       + \left(1 + 2 R - \nu w_0 (1-\lambda)\right) E_k^{n+1} \\
    &\qquad\qquad + \left(-R + \nu w_0 (1-\lambda/2) \right) E_{k+1}^{n+1}  = E_k^n.\tag{prevonN}
    @f}
    
The heart of the von Neumann analysis is the substitution of a growing or decaying
(in time index @f$n@f$) oscillatory mode on the grid of spatial wave number @f$\mu@f$:

	@f[E_k^n = \sigma^n e^{i\mu\,(k\Delta z)}.@f]

Here @f$k\Delta z = z_k@f$ is a grid point.  Such a mode is a solution to (@ref prevon) 
if and only if

    @f{align*}
    \sigma\Big[  &(-R - \nu w_0(\lambda/2)) e^{-i\mu\Delta z}
    	              + (1 + 2 R - \nu w_0 (1-\lambda)) \\
    	           &\quad   + (-R  + \nu w_0 (\lambda/2)) e^{+i\mu\Delta z}
    	                    + \nu w_0 (1-\lambda) e^{+i\mu\Delta z} \Big] = 1.
    @f}
    
This equation reduces by standard manipulations to

	@f[\sigma = \frac{1}{1 + \left(4 R - 2 \nu w_0 (1-\lambda)\right)\cos^2(\mu \Delta z/2)
	                      + i\,\nu w_0 (1-\lambda/2)\sin(\mu\Delta z)}.@f]

Note @f$4 R - 2 \nu w_0 (1-\lambda) \ge 0@f$ without restrictions on 
numerical parameters @f$\Delta t@f$, @f$\Delta z@f$, because @f$w_0<0@f$ in the 
case under consideration.  Therefore

	@f[|\sigma|^2 = \frac{1}{\left[1 + \left(4 R - 2 \nu w_0 (1-\lambda)\right)
	                              \cos^2(\mu \Delta z/2)\right]^2
	                      + \left[\nu w_0 (1-\lambda/2)\sin(\mu\Delta z)\right]^2}.@f]

This positive number is less than one, so @f$|\sigma| < 1@f$.  It follows that all
modes decay exponentially.
**END OF PROOF OF 2.**

@subsection remarktwo Remark about our von Neumann stability analysis.

The constant @f$\lambda@f$ is carefully chosen in (@ref lambdachoice) so that the maximum principle 1 applies.  On the other hand, both the implicit first-order upwind and the implicit centered difference formulas have unconditional stability in the von Neumann sense.  The proof of case 2 above is thus a formality, merely showing that a convex combination of unconditionally stable (von Neumann sense) schemes is still unconditionally stable in the same sense.

@subsection maxconsequence Convergence: a consequence of the maximum principle.
 
If we define the pointwise numerical error @f$e_k^n = E_k^n - E(t_n,x_i,y_j,z_k)@f$,
where @f$E(\dots)@f$ is the unknown exact solution (exact enthalpy field) [@ref MortonMayers],
then (@ref formax) implies an equality of the form

	@f[A e_k^{n+1} = e_k^n + B_- e_{k-1}^{n+1} + B_+ e_{k+1}^{n+1} + \Delta t\, \tau_k^n@f]

where @f$\tau_k^n@f$ is the truncation error of the scheme and @f$A,B_\pm@f$ are nonnegative  coefficients, which need no detail for now other than to note that @f$1 + B_- + B_+ = A@f$.  Letting @f${\bar e}^n = \max_k |e_k^n|@f$ we have, because of the positivity of coefficients,
@anchor prebound

    @f{equation}
    A |e_k^{n+1}| \le {\bar e}^n + \left(B_- + B_+\right){\bar e}^{n+1} + \Delta t\,\bar\tau^n \tag{prebound}
    @f}
    
for all @f$k@f$, where @f$\bar\tau^n = \max_k |\tau_k^n|@f$.  Now let @f$k@f$ be the index for 
which @f$|e_k^{n+1}| = {\bar e}^{n+1}@f$.  For that @f$k@f$ we can replace @f$|e_k^{n+1}|@f$ in 
equation (@ref prebound) with @f${\bar e}^{n+1}@f$.  Subtracting the same quantity from 
each side of the resulting inequality gives

    @f[
    {\bar e}^{n+1} \le {\bar e}^n + \Delta t\,\bar\tau^n,
    @f]
    
It follows that @f$\bar e^n \le C \Delta t@f$, for some finite @f$C@f$, if @f$\bar e^0 = 0@f$
[@ref MortonMayers].  Thus a maximum principle for BOMBPROOF implies convergence 
in the standard way [@ref MortonMayers].  This convergence proof has the same
assumptions as case 1 in the theorem, and thus it only \e suggests convergence
in any broad range of glaciologically-interesting cases.


@subsection final Remark on nonzero source term.

Now recall we assumed in Theorem 1 that the entire "source" @f$\Phi_k^n@f$ was identically zero.
Of course this is not realistic.  What we understand is provable, however, is that if a
numerical scheme for a linear advection/conduction equation

	@f[u_t + A u_x = B u_{xx}@f]

is stable in the general sense of numerical schemes for partial differential equations
(%e.g. as defined in subsection 5.5 of [@ref MortonMayers]) then the same scheme is stable 
in the same general sense when applied to the equation with (linear) lower order terms:

	@f[u_t + A u_x = B u_{xx} + C u + D.@f]

A precise statement of this general fact is hard to find in the literature, 
to put it mildly, but theorem 2.2.3 of [@ref Strikwerda] is one interesting case
(@f$B=0@f$ and @f$D=0@f$).  But even the form we state with linear term (@f$C u + D@f$)
is not adequate to the job because of the strongly-nonlinear dependence of @f$\Phi@f$
on the temperature @f$T@f$ [@ref BBL].

Nonetheless the maximum principle is a highly-desirable form of stability because we can exclude "wiggles" from the finite difference approximations of the conductive and advective terms, even if the complete physics, with strain heating in particular, is not yet shown to be non-explosive.  Because the complete physics includes the appearance of the famous "spokes" of EISMINT II, for example, a maximum principle cannot apply too literally.  Indeed there is an underlying fluid instability [@ref BBL], one that means the solution of the continuum equations can include growing "wiggles" which are fluid features (though not at the grid-based spatial frequency of the usual numerical wiggles).  Recall that, because we use first-order upwinding on the horizontal advection terms, we can expect maximum principle-type stability behavior of the whole three-dimensional scheme.

@section melt Temperate basal boundary condition, and computing the basal melt rate

At the bottom of grounded ice, a certain amount of heat comes out of the earth and either enters the ice through conduction or melts the base of the ice.  On the one hand, see the documentation for PISMBedThermalUnit for the model of how much comes out of the earth.  On the other hand, [@ref AschwandenBuelerKhroulevBlatter] includes a careful analysis of the subglacial layer equation and the corresponding boundary conditions and basal melt rate calculation, and the reader should consult that reference.

@subsection refloating Regarding the floating case

The shelf base temperature @f$ T_{sb} @f$ is supplied by PISMOceanModel::shelf_base_temperature().  The melt rate @f$M@f$ is supplied as a boundary condition from the ocean model by PISMOceanModel::shelf_base_mass_flux().  Note that we make the possibly-peculiar physical choice that the shelf base temperature is used as the temperature at the *top of the bedrock*, which is actually the bottom of the ocean.  This choice means that there should be no abrupt changes in top-of-bedrock heat flux as the grounding line moves.  This choice also means that the conservation of energy code does not need to know about the bedrock topography or the elevation of sea level.  (In the future there could be a PISMOceanModel::subshelf_bed_temperature() routine.)

