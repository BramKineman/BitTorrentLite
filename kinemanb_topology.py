from mininet.cli import CLI
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.topo import Topo
from mininet.log import setLogLevel

class AssignmentNetworks(Topo):
    def __init__(self, **opts):
        Topo.__init__(self, **opts)
    #Set up Network Here
        

        # Hosts
        hostOne = self.addHost('h1')
        hostTwo = self.addHost('h2')
        hostThree = self.addHost('h3')
        hostFour = self.addHost('h4')
        hostFive = self.addHost('h5')
        hostSix = self.addHost('h6')

        # Switches
        switchOne = self.addSwitch('s1')

        # Links
        # Switch 1 Links
        self.addLink(switchOne, hostOne)
        self.addLink(switchOne, hostTwo)
        self.addLink(switchOne, hostThree)
        self.addLink(switchOne, hostFour)
        self.addLink(switchOne, hostFive)
        self.addLink(switchOne, hostSix)


if __name__ == '__main__':
    setLogLevel( 'info' )

    # Create data network
    topo = AssignmentNetworks()
    net = Mininet(topo=topo, link=TCLink, autoSetMacs=True,
           autoStaticArp=True)

    # Run network
    net.start()
    CLI( net )
    net.stop()
