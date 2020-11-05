# ANPV

**A**nother **N**ameless **P**icture **V**iewer

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

Note: Designing a nice and modern user interface is not my strength. Also, this is not the focus of this work. If you want this, have a look at [photoqt](https://photoqt.org/).

Also, this project is designed as **viewer**! A viewer does not support editing the images. If you want this, go for [Gwenview](https://userbase.kde.org/Gwenview) or [Gimp](https://www.gimp.org/).
