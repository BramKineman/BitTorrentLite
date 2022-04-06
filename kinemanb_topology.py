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
        hostSeven = self.addHost('h7')
        hostEight = self.addHost('h8')
        hostNine = self.addHost('h9')
        hostTen = self.addHost('h10')

        # Switches
        switchOne = self.addSwitch('s1')
        switchTwo = self.addSwitch('s2')
        switchThree = self.addSwitch('s3')
        switchFour = self.addSwitch('s4')
        switchFive = self.addSwitch('s5')
        switchSix = self.addSwitch('s6')

        # Links
        # Delay / Bandwidth Options
        linkSOneSTwoOpts = dict(bw=20, delay='40ms')
        linkSTwoSThreeOpts = dict(bw=30, delay='20ms')
        linkSOneSFourOpts = dict(bw=40, delay='50ms')
        linkSTwoSFiveOpts = dict(bw=25, delay='5ms')
        linkSFiveSSixOpts = dict(bw=40, delay='5ms')
        # Inner Switch Links
        self.addLink(switchOne, switchTwo, **linkSOneSTwoOpts)
        self.addLink(switchTwo, switchThree, **linkSTwoSThreeOpts)
        self.addLink(switchOne, switchFour, **linkSOneSFourOpts)
        self.addLink(switchTwo, switchFive, **linkSTwoSFiveOpts)
        self.addLink(switchFive, switchSix, **linkSFiveSSixOpts)
        # Switch 1 Links
        self.addLink(switchOne, hostOne)
        self.addLink(switchOne, hostThree)
        self.addLink(switchOne, hostFour)
        # Switch 2 Links
        self.addLink(switchTwo, hostSix)
        # Switch 3 Links
        self.addLink(switchThree, hostEight)
        # Switch 4 Links
        self.addLink(switchFour, hostTwo)
        self.addLink(switchFour, hostFive)
        # Switch 5 Links
        self.addLink(switchFive, hostSeven)
        # Switch 6 Links
        self.addLink(switchSix, hostNine)
        self.addLink(switchSix, hostTen)


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