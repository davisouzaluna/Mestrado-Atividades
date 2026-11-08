import qrcode

dados = "https://github.com/davisouzaluna/Mestrado-Atividades/tree/main/Estatistica/friedman-sorting/friedman-sorting" 

imagem = qrcode.make(dados)
imagem.save("meu_qrcode.png")
print("QR Code gerado com sucesso!")



#Nao quero encher o requirements txt.
#Portanto, para gerar o qr, lembre-se de entrar num ambiente virtual do python(venv)
#E instalar com o pip a lib, com o comando abaixo:
# pip install qrcode pillow
