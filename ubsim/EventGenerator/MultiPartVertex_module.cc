////////////////////////////////////////////////////////////////////////
// Class:       MultiPartVertex
// Module Type: producer
// File:        MultiPartVertex_module.cc
//
// Generated at Tue Dec 13 15:48:59 2016 by Kazuhiro Terao using artmod
// from cetpkgsupport v1_11_00.
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
//#include "art/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include <memory>
#include <vector>

#include "CLHEP/Random/RandFlat.h"
#include "TRandom.h"
#include "nurandom/RandomUtils/NuRandomService.h"
#include "larcore/Geometry/Geometry.h"
#include "larcoreobj/SummaryData/RunData.h"

#include "nusimdata/SimulationBase/MCTruth.h"
#include "nusimdata/SimulationBase/MCParticle.h"

#include "TLorentzVector.h"
#include "TDatabasePDG.h"

struct PartGenParam {
  std::vector<int       > pdg;
  std::vector<double    > mass;
  std::array <size_t, 2 > multi;
  std::array <double, 2 > kerange;
  bool use_mom;
  double weight;
};

class MultiPartVertex : public art::EDProducer {
public:
  explicit MultiPartVertex(fhicl::ParameterSet const & p);

  // Plugins should not be copied or assigned.
  MultiPartVertex(MultiPartVertex const &) = delete;
  MultiPartVertex(MultiPartVertex &&) = delete;
  MultiPartVertex & operator = (MultiPartVertex const &) = delete;
  MultiPartVertex & operator = (MultiPartVertex &&) = delete;

private:
  // Required functions.
  void produce(art::Event & e) override;

  void beginRun(art::Run& run) override;

  void GenPosition(double& x, double& y, double& z);

  void GenMomentum(const PartGenParam& param, const double& mass, double& px, double& py, double& pz);

  std::vector<size_t> GenParticles();

  CLHEP::RandFlat _flatRandom;

  // exception thrower
  void abort(const std::string msg) const;

  // array of particle info for generation
  std::vector<PartGenParam> _param_v;

  // g4 time of generation
  double _t0;
  double _t0_sigma;

  // g4 position
  std::array<double,2> _xrange;
  std::array<double,2> _yrange;
  std::array<double,2> _zrange;

  // multiplicity constraint
  size_t _multi_min;
  size_t _multi_max;

  // verbosity flag
  unsigned short _debug;
};

void MultiPartVertex::abort(const std::string msg) const
{
  std::cerr << "\033[93m" << msg.c_str() << "\033[00m" << std::endl;
  throw std::exception();
}

MultiPartVertex::MultiPartVertex(fhicl::ParameterSet const & p)
  : EDProducer{p}
  // create a default random engine; obtain the random seed from NuRandomService,
  // unless overridden in configuration with key "Seed"
  , _flatRandom(art::ServiceHandle<rndm::NuRandomService>()->createEngine(*this, p, "Seed"))
{
  produces< std::vector<simb::MCTruth>   >();
  produces< sumdata::RunData, art::InRun >();

  _debug = p.get<unsigned short>("DebugMode",0);
  
  _t0 = p.get<double>("G4Time");
  _t0_sigma = p.get<double>("G4TimeJitter");
  if(_t0_sigma < 0) this->abort("Cannot have a negative value for G4 time jitter");

  _multi_min = p.get<size_t>("MultiMin");
  _multi_max = p.get<size_t>("MultiMax");

  auto const xrange    = p.get<std::vector<double> > ("XRange");
  auto const yrange    = p.get<std::vector<double> > ("YRange");
  auto const zrange    = p.get<std::vector<double> > ("ZRange");

  auto const part_cfg = p.get<fhicl::ParameterSet>("ParticleParameter");

  _param_v.clear();
  auto const pdg_v      = part_cfg.get<std::vector<std::vector<int>    > > ("PDGCode");
  auto const minmult_v  = part_cfg.get<std::vector<unsigned short> > ("MinMulti"); 
  auto const maxmult_v  = part_cfg.get<std::vector<unsigned short> > ("MaxMulti"); 
  auto const weight_v   = part_cfg.get<std::vector<double> > ("ProbWeight"); 

  auto kerange_v  = part_cfg.get<std::vector<std::vector<double> > > ("KERange");
  auto momrange_v = part_cfg.get<std::vector<std::vector<double> > > ("MomRange");

  if( (kerange_v.empty() && momrange_v.empty()) ||
      (!kerange_v.empty() && !momrange_v.empty()) ) {
    this->abort("Only one of KERange or MomRange must be empty!");
  }      
  
  bool use_mom = false;
  if(kerange_v.empty()){
    kerange_v = momrange_v;
    use_mom = true;
  } 
  // sanity check
  if( pdg_v.size() != kerange_v.size() ||
      pdg_v.size() != minmult_v.size() ||
      pdg_v.size() != maxmult_v.size() ||
      pdg_v.size() != weight_v.size() ) 
    this->abort("configuration parameters have incompatible lengths!");

  // further sanity check (1 more depth for some double-array)
  for(auto const& r : pdg_v    ) { if(              r.empty()  ) this->abort("PDG code not given!");                        }
  for(auto const& r : kerange_v) { if(              r.size()!=2) this->abort("Incompatible legnth @ KE vector!");           }
  
  for(size_t idx=0; idx<minmult_v.size(); ++idx) {
    if(minmult_v[idx] > maxmult_v[idx]) this->abort("Particle MinMulti > Particle MaxMulti!");
    if(minmult_v[idx] > _multi_max) this->abort("Particle MinMulti > overall MultiMax!");
    if(minmult_v[idx] > _multi_min)
      _multi_min = minmult_v[idx];
  }
  if(_multi_max < _multi_min) this->abort("Overall MultiMax <= overall MultiMin!");

  if(!xrange.empty() && xrange.size() >2) this->abort("Incompatible legnth @ X vector!" );
  if(!yrange.empty() && yrange.size() >2) this->abort("Incompatible legnth @ Y vector!" );
  if(!zrange.empty() && zrange.size() >2) this->abort("Incompatible legnth @ Z vector!" );

  // range register
  art::ServiceHandle<geo::Geometry> geo;
  _xrange[0] = 0.;
  _xrange[1] = 2. * geo->DetHalfWidth();
  _yrange[0] = -1. * geo->DetHalfHeight();
  _yrange[1] =  1. * geo->DetHalfHeight();
  _zrange[0] = 0.;
  _zrange[1] = geo->DetLength();
  if(xrange.size()==1) { _xrange[0] += xrange[0]; _xrange[1] -= xrange[0]; }
  if(xrange.size()==2) { _xrange[0]  = xrange[0]; _xrange[1]  = xrange[1]; }
  if(yrange.size()==1) { _yrange[0] += yrange[0]; _yrange[1] -= yrange[0]; }
  if(yrange.size()==2) { _yrange[0]  = yrange[0]; _yrange[1]  = yrange[1]; }
  if(zrange.size()==1) { _zrange[0] += zrange[0]; _zrange[1] -= zrange[0]; }
  if(zrange.size()==2) { _zrange[0]  = zrange[0]; _zrange[1]  = zrange[1]; }

  if(_xrange[0]  > _xrange[1] ) this->abort("X range has no phase space...");
  if(_yrange[0]  > _yrange[1] ) this->abort("Y range has no phase space...");
  if(_zrange[0]  > _zrange[1] ) this->abort("Z range has no phase space...");

  if(_debug>0) {
    std::cout << "Vertex is uniformly generated within following ranges:" << std::endl
	      << "    X  range ....... " << _xrange[0]  << " => " << _xrange[1]  << " [cm] " << std::endl
	      << "    Y  range ....... " << _yrange[0]  << " => " << _yrange[1]  << " [cm] " << std::endl
	      << "    Z  range ....... " << _zrange[0]  << " => " << _zrange[1]  << " [cm] " << std::endl
	      << std::endl;
  }

  // register
  auto db = new TDatabasePDG;
  for(size_t idx=0; idx<pdg_v.size(); ++idx) {
    auto const& pdg     = pdg_v[idx];
    auto const& kerange = kerange_v[idx];
    PartGenParam param;
    param.use_mom    = use_mom;
    param.pdg        = pdg;
    param.kerange[0] = kerange[0];
    param.kerange[1] = kerange[1];
    param.mass.resize(pdg.size());
    param.multi[0]   = minmult_v[idx];
    param.multi[1]   = maxmult_v[idx];
    param.weight     = weight_v[idx];
    for(size_t i=0; i<pdg.size(); ++i)
      param.mass[i] = db->GetParticle(param.pdg[i])->Mass();
    
    // sanity check
    if(kerange[0]<0 || kerange[1]<0)
      this->abort("You provided negative energy? Fuck off Mr. Trump.");

    // overall range check
    if(param.kerange[0] > param.kerange[1]) this->abort("KE range has no phase space...");

    if(_debug>0) {
      std::cout << "Generating particle (PDG";
      for(auto const& pdg : param.pdg) std::cout << " " << pdg;
      std::cout << ")" << std::endl
		<< (param.use_mom ? "    KE range ....... " : "    Mom range ....... ") 
		<< param.kerange[0] << " => " << param.kerange[1] << " MeV" << std::endl
		<< std::endl;
    }

    _param_v.push_back(param);
  }
}

void MultiPartVertex::beginRun(art::Run& run)
{
  // grab the geometry object to see what geometry we are using
  art::ServiceHandle<geo::Geometry> geo;

  std::unique_ptr<sumdata::RunData> runData(new sumdata::RunData(geo->DetectorName()));

  run.put(std::move(runData));
}

std::vector<size_t> MultiPartVertex::GenParticles() {

  std::vector<size_t> result;
  std::vector<size_t> gen_count_v(_param_v.size(),0);
  std::vector<double> weight_v(_param_v.size(),0);
  for(size_t idx=0; idx<_param_v.size(); ++idx)
    weight_v[idx] = _param_v[idx].weight;
  
  int num_part = (int)(_flatRandom.fire(_multi_min,_multi_max+1-1.e-10));
  
  while(num_part) {

    double total_weight = 0;
    for(auto const& v : weight_v) total_weight += v;

    double rval = 0;
    rval = _flatRandom.fire(0,total_weight);

    size_t idx = 0;
    for(idx = 0; idx < weight_v.size(); ++idx) {
      rval -= weight_v[idx];
      if(rval <=0.) break;
    }

    // register to the output
    result.push_back(idx);

    // if generation count exceeds max, set probability weight to be 0
    gen_count_v[idx] += 1;
    if(gen_count_v[idx] >= _param_v[idx].multi[1])
      weight_v[idx] = 0.;
    
    --num_part;
  }
  return result;
}

void MultiPartVertex::GenPosition(double& x, double& y, double& z) {

  std::vector<double> pos(3,0);

  x = _flatRandom.fire(_xrange[0], _xrange[1]);
  y = _flatRandom.fire(_yrange[0], _yrange[1]);
  z = _flatRandom.fire(_zrange[0], _zrange[1]);

}

void MultiPartVertex::GenMomentum(const PartGenParam& param, const double& mass, double& px, double& py, double& pz) {

  double tot_energy = 0;
  if(param.use_mom) 
    tot_energy = sqrt(pow(_flatRandom.fire(param.kerange[0],param.kerange[1]),2) + pow(mass,2));
  else
    tot_energy = _flatRandom.fire(param.kerange[0],param.kerange[1]) + mass;

  double mom_mag = sqrt(pow(tot_energy,2) - pow(mass,2));

  double phi   = _flatRandom.fire(0, 2 * 3.141592653589793238);
  double theta = _flatRandom.fire(0, 1 * 3.141592653589793238);

  px = cos(phi) * sin(theta);
  py = sin(phi) * sin(theta);
  pz = cos(theta);

  std::cout<<"LOGME,"<<phi<<","<<theta<<","<<px<<","<<py<<","<<pz<<std::endl;

  if(_debug>1)
    std::cout << "    Direction : (" << px << "," << py << "," << pz << ")" << std::endl
	      << "    Momentum  : " << mom_mag << " [MeV/c]" << std::endl
	      << "    Energy    : " << tot_energy << " [MeV/c^2]" << std::endl;
  px *= mom_mag;
  py *= mom_mag;
  pz *= mom_mag;

}

void MultiPartVertex::produce(art::Event & e)
{
  if(_debug>0) std::cout << "Processing a new event..." << std::endl;

  std::unique_ptr< std::vector<simb::MCTruth> > mctArray(new std::vector<simb::MCTruth>);

  double g4_time = _flatRandom.fire(_t0 - _t0_sigma/2., _t0 + _t0_sigma/2.);

  double x, y, z;
  GenPosition(x,y,z);
  TLorentzVector pos(x,y,z,g4_time);
  
  simb::MCTruth mct;

  mct.SetOrigin(simb::kBeamNeutrino);

  std::vector<simb::MCParticle> part_v;

  auto const param_idx_v = GenParticles();
  if(_debug)
    std::cout << "Event Vertex @ (" << x << "," << y << "," << z << ") ... " << param_idx_v.size() << " particles..." << std::endl;

  for(size_t idx=0; idx<param_idx_v.size(); ++idx) {
    auto const& param = _param_v[param_idx_v[idx]];
    double px,py,pz;
    // decide which particle
    size_t pdg_index = (size_t)(_flatRandom.fire(0,param.pdg.size()-1.e-10));
    auto const& pdg  = param.pdg[pdg_index];
    auto const& mass = param.mass[pdg_index];
    if(_debug) std::cout << "  " << idx << "th instance PDG " << pdg << std::endl;
    GenMomentum(param,mass,px,py,pz);
    TLorentzVector mom(px,py,pz,sqrt(pow(px,2)+pow(py,2)+pow(pz,2)+pow(mass,2)));
    simb::MCParticle part(mct.NParticles(), pdg, "primary", 0, mass, 1);
    part.AddTrajectoryPoint(pos,mom);
    part_v.emplace_back(std::move(part));
  }

  if(_debug) std::cout << "Total number particles: " << mct.NParticles() << std::endl;

  simb::MCParticle nu(mct.NParticles(), 16, "primary", mct.NParticles(), 0, 0);
  double px=0;
  double py=0;
  double pz=0;
  double en=0;
  for(auto const& part : part_v) {
    px = part.Momentum().Px();
    py = part.Momentum().Py();
    pz = part.Momentum().Pz();
    en = part.Momentum().E();
  }
  TLorentzVector mom(px,py,pz,en);
  nu.AddTrajectoryPoint(pos,mom);
  
  mct.Add(nu);
  for(auto& part : part_v)
    mct.Add(part);

  mctArray->push_back(mct);
  
  e.put(std::move(mctArray));
}

DEFINE_ART_MODULE(MultiPartVertex)
