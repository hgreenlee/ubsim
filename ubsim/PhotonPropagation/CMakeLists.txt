cet_build_plugin(
  UBPhotonLibraryPropagation art::EDProducer
  LIBRARIES
  PRIVATE
  larsim::IonizationScintillation
  larsim::LegacyLArG4
  larsim::PhotonPropagation_PhotonVisibilityService_service
  larevt::SpaceChargeService
  lardata::LArPropertiesService
  larcore::Geometry_Geometry_service
  nurandom::RandomUtils_NuRandomService_service
)

install_fhicl()
install_source()
