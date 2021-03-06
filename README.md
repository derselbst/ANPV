# ANPV

**A**nother **N**ameless **P**icture **V**iewer

[![Build Status](https://dev.azure.com/tommbrt/tommbrt/_apis/build/status/derselbst.ANPV?branchName=master)](https://dev.azure.com/tommbrt/tommbrt/_build/latest?definitionId=7&branchName=master)

# Features

ANPV is my attempt to write a fast, responsive and smart image viewer. The primary goal is using it to sort my image collection.

What makes this image viewer different? In short: ANPV decodes images in a smart way:

* Decoding is **not** done in the **UI thread**, therefore user input is not blocked at any time
* Images are **decoded incrementally**, if possible (Progressive JPEGs!!!)
* The decoding progress is actually visualized
* **EXIF** metadata is honored
* **Embedded thumbnails** are used to provide an instant preview of the actual full scale image, if available
* Decoding of images can be *cancelled*
* **Big panorama images** are decoded and viewed in an efficient way

I haven't found an image viewer that follows those simple design principles. Originally, I wanted to improve Gwenview. But nobody paied attention to my pull requests for 2 months. What a waste of time. Hence I decided to write my own one.

The goal is not to use new, fancy decoders that squeeze out every millisecond of decoding time. The goal is to keep the UI updated and responsive.

One note about TIFF files: There are so many possible subformats of TIFF, that it can be hardly tested all: Tile-based, strip-based, separate, planar, etc. Avoid using JPEG compressed TIFF files. JPEGs should be stored in JPEG files. TIFF files can also be multi-layered: The core assumption of ANPV is that one image file contains one and only one picture. Perhaps at different resolutions. Those artifically generated multi-layered TIFF files where each layer contains a different image are not considered to be a real world use-case. Hence, they are guaranteed to display correctly. In a multi-layered TIFF file, always the image with the biggest available resolution will be decoded. The smallest resolution image might be used as thumbnail preview.

Also, this project is designed as **viewer**! A viewer does not support editing the images. If you want this, go for [Gwenview](https://userbase.kde.org/Gwenview) or [Gimp](https://www.gimp.org/). Designing a nice and modern user interface, on the other hand, is not my strength. It is not the focus of this work either. If you want this, have a look at [photoqt](https://photoqt.org/).
