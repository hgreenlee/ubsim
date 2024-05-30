////////////////////////////////////////////////////////////////////////
// Class:       UBPhotonLibraryPropagation
// Plugin Type: producer (art v2_05_00)
// File:        UBPhotonLibraryPropagation_module.cc
//
// Generated at Tue Mar 21 07:45:42 2017 by Wesley Ketchum using cetskelgen
// from cetlib version v1_21_00.
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art/Framework/Services/Optional/RandomNumberGenerator.h"
#include "nutools/RandomUtils/NuRandomService.h"

#include <memory>
#include <iostream>

#include "larcore/Geometry/Geometry.h"
#include "larsim/PhotonPropagation/PhotonVisibilityService.h"
#include "larsim/Simulation/PhotonVoxels.h"

#include "CLHEP/Random/RandFlat.h"
#include "CLHEP/Random/RandPoissonQ.h"

#include "lardataobj/Simulation/SimPhotons.h"
#include "lardataobj/Simulation/SimEnergyDeposit.h"
#include "larsim/IonizationScintillation/ISCalcSeparate.h"

#include "lardata/DetectorInfoServices/LArPropertiesService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "larsim/Simulation/LArG4Parameters.h"
#include "larevt/SpaceChargeServices/SpaceChargeService.h"


namespace phot {
  class UBPhotonLibraryPropagation;
}


class phot::UBPhotonLibraryPropagation : public art::EDProducer {
public:
  explicit UBPhotonLibraryPropagation(fhicl::ParameterSet const & p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  UBPhotonLibraryPropagation(UBPhotonLibraryPropagation const &) = delete;
  UBPhotonLibraryPropagation(UBPhotonLibraryPropagation &&) = delete;
  UBPhotonLibraryPropagation & operator = (UBPhotonLibraryPropagation const &) = delete;
  UBPhotonLibraryPropagation & operator = (UBPhotonLibraryPropagation &&) = delete;

  // Required functions.
  void produce(art::Event & e) override;

  // Selected optional functions.
  void reconfigure(fhicl::ParameterSet const & p);
  void beginJob() override;

private:

  double fRiseTimeFast;
  double fRiseTimeSlow;
  bool   fDoSlowComponent;

  std::vector<art::InputTag> fEDepTags;
  std::vector<double> fPhotonScale;

  larg4::ISCalcSeparate fISAlg;

  double GetScintYield(sim::SimEnergyDeposit const&, detinfo::LArProperties const&);

  double GetScintTime(double scint_time, double rise_time, double, double);
};


phot::UBPhotonLibraryPropagation::UBPhotonLibraryPropagation(fhicl::ParameterSet const & p) : art::EDProducer(p)
{
  produces< std::vector<sim::SimPhotons> >();
  (void)art::ServiceHandle<rndm::NuRandomService>()->createEngine(*this, "HepJamesRandom", "photon",    p, "SeedPhoton");
  (void)art::ServiceHandle<rndm::NuRandomService>()->createEngine(*this, "HepJamesRandom", "scinttime", p, "SeedScintTime");
  //art::ServiceHandle<rndm::NuRandomService>()->createEngine(*this, "HepJamesRandom", "photon",    p, "SeedPhoton");
  //art::ServiceHandle<rndm::NuRandomService>()->createEngine(*this, "HepJamesRandom", "scinttime", p, "SeedScintTime");  
  this->reconfigure(p);
}

double phot::UBPhotonLibraryPropagation::GetScintYield(sim::SimEnergyDeposit const& edep,
						     detinfo::LArProperties const& larp)
{
  double yieldRatio = larp.ScintYieldRatio();
  if(larp.ScintByParticleType()){
    switch(edep.PdgCode()) {
    case 2212:
      yieldRatio = larp.ProtonScintYieldRatio();
      break;
    case 13:
    case -13:
      yieldRatio = larp.MuonScintYieldRatio();
      break;
    case 211:
    case -211:
      yieldRatio = larp.PionScintYieldRatio();
      break;
    case 321:
    case -321:
      yieldRatio = larp.KaonScintYieldRatio();
      break;
    case 1000020040:
      yieldRatio = larp.AlphaScintYieldRatio();
      break;
    case 11:
    case -11:
    case 22:
      yieldRatio = larp.ElectronScintYieldRatio();
      break;
    default:
      yieldRatio = larp.ElectronScintYieldRatio();
    }
  }
  return yieldRatio;
}

void phot::UBPhotonLibraryPropagation::produce(art::Event & e)
{
  art::ServiceHandle<PhotonVisibilityService> pvs;
  art::ServiceHandle<sim::LArG4Parameters> lgpHandle;
  const detinfo::LArProperties* larp = lar::providerFrom<detinfo::LArPropertiesService>();
  
  art::ServiceHandle<art::RandomNumberGenerator> rng;  

  auto const& module_label = moduleDescription().moduleLabel();

  //CLHEP::HepRandomEngine &engine_photon = rng->getEngine("photon");
  auto& engine_photon = rng->getEngine(art::ScheduleID::first(), module_label, "photon");
  CLHEP::RandPoissonQ randpoisphot(engine_photon);
  //CLHEP::HepRandomEngine &engine_scinttime = rng->getEngine("scinttime");
  auto& engine_scinttime = rng->getEngine(art::ScheduleID::first(), module_label, "scinttime");
  CLHEP::RandFlat randflatscinttime(engine_scinttime);
  
  const size_t NOpChannels = pvs->NOpChannels();
  double yieldRatio;
  double nphot,nphot_fast,nphot_slow;

  //auto fSCE = lar::providerFrom<spacecharge::SpaceChargeService>();

  sim::OnePhoton photon;
  photon.Energy = 9.7e-6;
  photon.SetInSD = false;
  
  fISAlg.Initialize(larp,
		    lar::providerFrom<detinfo::DetectorPropertiesService>(),
		    &(*lgpHandle),
		    lar::providerFrom<spacecharge::SpaceChargeService>());

  std::unique_ptr< std::vector<sim::SimPhotons> > photCol ( new std::vector<sim::SimPhotons>);
  auto & photonCollection(*photCol);

  //size_t edep_reserve_size=0;
  std::vector< std::vector<sim::SimEnergyDeposit> const*> edep_vecs;
  for(auto label : fEDepTags){
    auto const& edep_handle = e.getValidHandle< std::vector<sim::SimEnergyDeposit> >(label);
    edep_vecs.push_back(edep_handle);
    //edep_reserve_size += edep_handle->size();
  }

  for(size_t i_op=0; i_op<NOpChannels; ++i_op){
    photonCollection.emplace_back(i_op);
  }

  size_t nvec = 0;
  for(auto const& edeps : edep_vecs){
    double scale_factor = fPhotonScale[nvec++];

    for(auto const& edep : *edeps){
      /*
      std::cout << "Processing edep with trackID=" 
		<< edep.TrackID()
		<< " pdgCode="
		<< edep.PdgCode() 
		<< " energy="
		<< edep.Energy()
		<< "(x,y,z)=("
		<< edep.X() << "," << edep.Y() << "," << edep.Z() << ")"
		<< std::endl;
      */
      double const xyz[3] = { edep.X(), edep.Y(), edep.Z() };
      
      photon.InitialPosition = TVector3(xyz[0],xyz[1],xyz[2]);
      
      float const* Visibilities = pvs->GetAllVisibilities(xyz);
      if(!Visibilities)
	continue;
      
      yieldRatio = GetScintYield(edep,*larp);
      fISAlg.Reset();
      fISAlg.CalculateIonizationAndScintillation(edep);
      nphot =fISAlg.NumberScintillationPhotons();
      nphot_fast = yieldRatio*nphot;

      photon.Time = edep.T() + GetScintTime(larp->ScintFastTimeConst(),fRiseTimeFast,
					    randflatscinttime(),randflatscinttime());
      //std::cout << "\t\tPhoton fast time is " << photon.Time << " (" << edep.T() << " orig)" << std::endl;
      for(size_t i_op=0; i_op<NOpChannels; ++i_op){
	auto nph = randpoisphot.fire(nphot_fast*Visibilities[i_op]*scale_factor);
	/*
	  std::cout << "\t\tHave " << nph << " fast photons ("
	  << Visibilities[i_op] << "*" << nphot_fast << " from " << nphot << ")"
	  << " for opdet " << i_op << std::endl;
	  //photonCollection[i_op].insert(photonCollection[i_op].end(),randpoisphot.fire(nphot_fast*Visibilities[i_op]),photon);
	  */
	photonCollection[i_op].insert(photonCollection[i_op].end(),nph,photon);
      }
      if(fDoSlowComponent){
	nphot_slow = nphot - nphot_fast;
	
	if(nphot_slow>0){
	  photon.Time = edep.T() + GetScintTime(larp->ScintSlowTimeConst(),fRiseTimeSlow,
						randflatscinttime(),randflatscinttime());
	  //std::cout << "\t\tPhoton slow time is " << photon.Time << " (" << edep.T() << " orig)" << std::endl;
	  for(size_t i_op=0; i_op<NOpChannels; ++i_op){
	    auto nph = randpoisphot.fire(nphot_slow*Visibilities[i_op]*scale_factor);
	    //std::cout << "\t\tHave " << nph << " slow photons ("
	    //	      << Visibilities[i_op] << "*" << nphot_slow << " from " << nphot << ")"
	    //	      << " for opdet " << i_op << std::endl;
	    photonCollection[i_op].insert(photonCollection[i_op].end(),nph,photon);
	  }
	}
	
      }//end doing slow component
      
    }//end loop over edeps
  }//end loop over edep vectors
  
  e.put(std::move(photCol));
  
}

double phot::UBPhotonLibraryPropagation::GetScintTime(double scint_time, double rise_time,
						    double r1, double r2)
{
  //no rise time
  if(rise_time<0.0)
    return -1 * scint_time * std::log(r1);

  while(1){
    double t = -1.0*scint_time*std::log(1-r1);
    //double g = (scint_time+rise_time)/scint_time * std::exp(-1.0*t/scint_time)/scint_time;
    if ( r2 <= (std::exp(-1.0*t/rise_time)*(1-std::exp(-1.0*t/rise_time))/scint_time/scint_time*(scint_time+rise_time)) )
      return -1 * t;
  }
  
  return 1;
  
}

void phot::UBPhotonLibraryPropagation::reconfigure(fhicl::ParameterSet const & p)
{
  fRiseTimeFast = p.get<double>("RiseTimeFast",-1.0);
  fRiseTimeSlow = p.get<double>("RiseTimeSlow",-1.0);
  fDoSlowComponent = p.get<bool>("DoSlowComponent");

  fEDepTags = p.get< std::vector<art::InputTag> >("EDepModuleLabels");
  fPhotonScale = p.get< std::vector<double> >("PhotonScale", std::vector<double>());
  while(fPhotonScale.size() < fEDepTags.size())
    fPhotonScale.push_back(1.);
}

void phot::UBPhotonLibraryPropagation::beginJob()
{

}

DEFINE_ART_MODULE(phot::UBPhotonLibraryPropagation)

