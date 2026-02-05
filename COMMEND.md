# Commend BareSIP Fork

The goal of the Commend BareSIP fork is to keep Commend-specific commits to a
minimum and staying as close to mainline as possible.

## Workflow For Integrating Upstream Changes

We need to regularly integrate the upstream BareSIP changes into our Commend
fork. This is done by regularly rebasing our `sprint` branch onto the upstream
`main` branch. Then, the new sprint branch is force-pushed to our Commend Azure
remote (ideally with `git push --force-with-lease`). This ensures that
Commend-specific commits are always at the top of the git history.

## Commend Commits Not For Mainline

To list the commits which are Commend-specific, you can run for example

```bash
git log --pretty=format:"%h - %as - %an: %s" sprint ^main
```

- commod

    Commend specific commands and UA event filter. General interesting modules
    should go to the repository `baresip-apps` which should be free of Commend
    specific commits.

- comvideo

    It is very special and coupled tightly to the camerad for SYMX.

## Commend Modules Already Removed
- onvif (SyBF only)

    A commend module. Makes no sense for non-SYBF devices.

    last branch:    `pre_v4.5.0_backup`
    used in branch: `symx_04.01.xx`

- auogg (SyBF only)

    Currently supports only the deprecated speex codec. No one (at Commend) is
    interested on an auogg module that supports a modern codec like opus.

    last branch:    `pre_v4.5.0_backup`
    used in branch: `symx_04.01.xx`

- speex (SyBF only)

    All commits that belong to the module speex which was removed mainline.
    The speex codec is deprecated. We use it only on SYBF devices.

    last branch:    `pre_v4.5.0_backup`
    used in branch: `symx_04.01.xx`

- alsa\_audiocore (SyBF only)

    The SYBF audio source and player module.

    last branch:    `pre_v4.5.0_backup`
    used in branch: `symx_04.01.xx`

- multicast (SyBF only)

    The SYBF part of multicast are not interesting for main line.

    last branch:    `pre_v4.5.0_backup`
    used in branch: `symx_04.01.xx`

- idlepipe (SyBF only)

    Used only on SYBF. Is not interesting for main line. It is used for onvif
    and the SYBF multicast solution.

    last branch:    `pre_v4.5.0_backup`
    used in branch: `symx_04.01.xx`

- ac\_symphony (SyBF only)

    The softclient (skidata) integration of the SYMX audiocore.

    last branch:    `pre_v4.5.0_backup`
    used in branch: `symx_04.01.xx`
