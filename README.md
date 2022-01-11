# FM-BISON

FM. BISON - A hybrid FM synthesis engine.

by bipolaraudio.nl
in cooperation with njdewit technologies/visualizersdotnl

# IMPORTANT

- Basic documentation will be written at some point; for now just explore our code, it has it's share of comments
- Github issue list is complete, if interested give it a once over as this project is under *heavy* development
- Currently there are 2 dependencies on JUCE 6.x (juce::dsp::Oversampling, juce::SmoothedValue)
- Not a lot of optimization has been done; it is reasonably fast, but since we are in R&D flexibility is more important
- All third-party code and resources (well, almost) we've used is credited on top of FM_BISON.h!
- Our internal R&D plug-in (which is pretty sweet and feature-complete) is available on request

A clean-up pass will be done in the near future as a lot of things have been added "here and there" as of late!

# FOLDER GUIDE

- /3rdparty: Third-party code (mostly adapted & modified, which in turn is commented on top of files)
- /helper: Codebase's "helper" functionality (basics included through synth-global.h)
- /quarantined: Code that must be phased out because it's not up to snuff
- /patch: FM. BISON's patch headers, laying out the entire structure an instance uses to render an instrument
- /promotion: Promotional material (graphics, audio renders et cetera)
- /literature: PDFs et cetera

# (OLD) TRAILER (30/04/2020)

https://youtu.be/hjkpSV5BxB0

# WHAT IS FM. BISON?

Combining proven entrepreneurial skills and over 2 decades of programming experience
we dipped our toes in synthesis and offer this first draft of our technology.

Our goal is to create efficient & high-fidelity synthesizer software.

What inspired FM. BISON is the 1980s digital revolution chiefly based on frequency modulation (or 'FM'),
as discovered by accident by John Chowning in 1967. This type of synthesis yields a very distinct
sound that helped artists like Prince, Phil Collins, Whitney Houston, Brian Eno and many others
to define their new sound, often using the Yamaha DX7.

We stand on the shoulders of thousands of creative innovators, engineers, artists *and* the public
domain and we invite you to profit from the work we are doing to take FM to another level.

Because FM synthesis is highly, notoriously so, programmable we aim to offer a wide variety of
possibilities: from rumbling analog bass lines to sound effects to patches that feel like you're 
playing an actual instrument.

Our code is open to study, disassemble, improve and use in any way you like, so long as you adhere
to the MIT license.

Please refer to the Github issue list to keep track of our to-do list, to see what we're aware of and working 
on and the new features or improvements we have in mind.

This code is currently in use/development for our in-house plug-ins.

Any significant contributions to this codebase will of course grant you a free copy!

Contact us if you're interested in any way or have questions: hello@bipolaraudio.nl

And if you use or benefit from our open development, we would love a shoutout on your socials, using #FM-BISON!
