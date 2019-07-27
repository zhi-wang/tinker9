#include "gpu/decl_mdstate.h"
#include "gpu/e_potential.h"
#include "util_potent.h"

TINKER_NAMESPACE_BEGIN
namespace gpu {
extern void polargroup_data(rc_t);
void potential_data(rc_t rc) {
  if ((use_data & vmask) == 0)
    return;

  egv_data(rc);

  // bonded terms

  ebond_data(rc);
  eangle_data(rc);
  estrbnd_data(rc);
  eurey_data(rc);
  eopbend_data(rc);
  etors_data(rc);
  epitors_data(rc);
  etortor_data(rc);

  // non-bonded terms

  evdw_data(rc);

  // Must call elec_data() before any electrostatics routine.

  elec_data(rc);
  empole_data(rc);
  if (use_potent(polar_term))
    polargroup_data(rc);
  epolar_data(rc);
}

void energy_potential(int vers) {

  zero_egv(vers);

  // bonded terms

  if (use_potent(bond_term))
    ebond(vers);
  if (use_potent(angle_term))
    eangle(vers);
  if (use_potent(strbnd_term))
    estrbnd(vers);
  if (use_potent(urey_term))
    eurey(vers);
  if (use_potent(opbend_term))
    eopbend(vers);
  if (use_potent(torsion_term))
    etors(vers);
  if (use_potent(pitors_term))
    epitors(vers);
  if (use_potent(tortor_term))
    etortor(vers);

  // non-bonded terms

  if (use_potent(vdw_term))
    evdw(vers);

  gpu::elec_init(vers);
  if (use_potent(mpole_term))
    empole(vers);
  if (use_potent(polar_term))
    epolar(vers);
  gpu::torque(vers);

  sum_energies(vers);

  // list update

  nblist_data(rc_evolve);
}
}
TINKER_NAMESPACE_END
